using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using Iced.Intel;

// Read-only PE64 analysis helper.
//   PeScan <exe> str <text>              RIP-relative references to an ASCII string
//   PeScan <exe> wstr <text>             same for a UTF-16 string
//   PeScan <exe> iat <dll> <name|#ord>   references to an IAT slot
//   PeScan <exe> rva <hexRva>            references to an arbitrary RVA
//   PeScan <exe> fn <hexRva>             disassemble the function containing an RVA
//   PeScan <exe> callers <hexRva>        direct calls to the function containing an RVA
//   PeScan <exe> imports                 every import with its IAT RVA
internal static class Program
{
    private static int Main(string[] args)
    {
        if (args.Length < 2)
        {
            Console.WriteLine("usage: PeScan <exe> <command> ...");
            return 1;
        }
        var pe = Pe.Load(args[0]);
        string cmd = args[1];
        switch (cmd)
        {
            case "imports":
                foreach (var imp in pe.Imports)
                    Console.WriteLine($"{imp.Dll}!{imp.Name} iat=0x{imp.IatRva:X}");
                return 0;
            case "str":
            case "wstr":
            {
                byte[] pat = cmd == "str" ? Encoding.ASCII.GetBytes(args[2]) : Encoding.Unicode.GetBytes(args[2]);
                int unit = cmd == "str" ? 1 : 2;
                var offs = pe.FindAll(pat);
                if (offs.Count == 0) { Console.WriteLine("string not found"); return 1; }
                foreach (int hit in offs.Take(6))
                {
                    int off = hit;
                    while (off - unit >= 0 && (unit == 1 ? pe.Data[off - 1] != 0 : (pe.Data[off - 2] | pe.Data[off - 1]) != 0)) off -= unit;
                    uint rva = pe.OffsetToRva((uint)off);
                    Console.WriteLine($"== string at file 0x{off:X} rva 0x{rva:X}");
                    pe.PrintRefs(rva, 1);
                }
                return 0;
            }
            case "iat":
            {
                string dll = args[2];
                string name = args[3];
                var hits = pe.Imports.Where(i => i.Dll.Equals(dll, StringComparison.OrdinalIgnoreCase) && i.Name == name).ToList();
                if (hits.Count == 0) { Console.WriteLine("import not found"); return 1; }
                foreach (var imp in hits)
                {
                    Console.WriteLine($"== {imp.Dll}!{imp.Name} iat 0x{imp.IatRva:X}");
                    pe.PrintRefs(imp.IatRva, 8);
                }
                return 0;
            }
            case "rva":
            {
                uint rva = ParseHex(args[2]);
                uint size = args.Length > 3 ? ParseHex(args[3]) : 1;
                pe.PrintRefs(rva, size);
                return 0;
            }
            case "fn":
            {
                uint rva = ParseHex(args[2]);
                var f = pe.FunctionOf(rva);
                if (f.Begin == 0) { Console.WriteLine("no pdata function"); return 1; }
                Console.WriteLine($"function 0x{pe.ImageBase + f.Begin:X}-0x{pe.ImageBase + f.End:X} size 0x{f.End - f.Begin:X}");
                foreach (var ins in pe.Decode(f))
                    Console.WriteLine(pe.Describe(ins));
                return 0;
            }
            case "dis":
            {
                uint rva = ParseHex(args[2]);
                uint len = args.Length > 3 ? ParseHex(args[3]) : 0x200;
                foreach (var ins in pe.Decode(new Func { Begin = rva, End = rva + len }))
                    Console.WriteLine(pe.Describe(ins));
                return 0;
            }
            case "scenes":
            {
                // Jump table of dword RVAs; each case allocates a scene and calls its constructor.
                uint table = ParseHex(args[2]);
                int count = int.Parse(args[3]);
                int toff = (int)pe.RvaToOffset(table);
                for (int i = 0; i < count; i++)
                {
                    uint target = BitConverter.ToUInt32(pe.Data, toff + i * 4);
                    var code = pe.Decode(new Func { Begin = target, End = target + 0x40 });
                    ulong ctor = 0;
                    int calls = 0;
                    foreach (var ins in code)
                    {
                        if (ins.FlowControl == FlowControl.Call && ins.Op0Kind == OpKind.NearBranch64)
                        {
                            calls++;
                            if (calls == 2) { ctor = ins.NearBranchTarget; break; }
                        }
                        if (ins.FlowControl == FlowControl.UnconditionalBranch) break;
                    }
                    string name = "";
                    if (ctor != 0)
                    {
                        uint c = (uint)(ctor - pe.ImageBase);
                        foreach (var ins in pe.Decode(new Func { Begin = c, End = c + 0x80 }))
                        {
                            if (!ins.IsIPRelativeMemoryOperand) continue;
                            string s = pe.CString((uint)(ins.IPRelativeMemoryAddress - pe.ImageBase), 60);
                            if (s.Length >= 3) { name = s; break; }
                        }
                    }
                    Console.WriteLine($"id 0x{i:X2} ({i,2}) case 0x{pe.ImageBase + target:X} ctor 0x{ctor:X} {name}");
                }
                return 0;
            }
            case "callargs":
            {
                // For each direct call to a function, print the instructions that set rcx/rdx/r8 just before it.
                uint rva = ParseHex(args[2]);
                foreach (var (site, fn) in pe.DirectCalls(rva))
                {
                    var code = pe.Decode(fn);
                    int idx = code.FindIndex(x => x.IP == pe.ImageBase + site);
                    if (idx < 0) { Console.WriteLine($"0x{pe.ImageBase + site:X} (undecodable)"); continue; }
                    var sb = new StringBuilder();
                    for (int k = idx - 1; k >= Math.Max(0, idx - 8); k--)
                    {
                        var ins = code[k];
                        if (ins.FlowControl != FlowControl.Next) break;
                        string t = ins.ToString();
                        if (t.Contains("edx") || t.Contains("rdx") || t.Contains("dl,") || t.Contains("ecx,") || t.Contains("r8d"))
                            sb.Insert(0, t + " | ");
                    }
                    Console.WriteLine($"0x{pe.ImageBase + site:X} in fn 0x{pe.ImageBase + fn.Begin:X}: {sb}");
                }
                return 0;
            }
            case "range":
            {
                uint from = ParseHex(args[2]);
                uint to = ParseHex(args[3]);
                foreach (var f in pe.Functions.Where(x => x.Begin >= from && x.Begin < to))
                {
                    Console.WriteLine();
                    Console.WriteLine($"==== function 0x{pe.ImageBase + f.Begin:X}-0x{pe.ImageBase + f.End:X} size 0x{f.End - f.Begin:X}");
                    foreach (var (site, caller) in pe.DirectCalls(f.Begin).Take(12))
                        Console.WriteLine($"     caller 0x{pe.ImageBase + site:X} in fn 0x{pe.ImageBase + caller.Begin:X}");
                    foreach (var ins in pe.Decode(f))
                        Console.WriteLine(pe.Describe(ins));
                }
                return 0;
            }
            case "callers":
            {
                uint rva = ParseHex(args[2]);
                var f = pe.FunctionOf(rva);
                uint target = f.Begin != 0 ? f.Begin : rva;
                Console.WriteLine($"callers of 0x{pe.ImageBase + target:X}");
                foreach (var (site, fn) in pe.DirectCalls(target))
                    Console.WriteLine($"  call at 0x{pe.ImageBase + site:X} in fn 0x{pe.ImageBase + fn.Begin:X} size 0x{fn.End - fn.Begin:X}");
                return 0;
            }
        }
        Console.WriteLine("unknown command");
        return 1;
    }

    private static uint ParseHex(string s)
    {
        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) s = s.Substring(2);
        ulong v = ulong.Parse(s, NumberStyles.HexNumber);
        if (v >= 0x140000000UL) v -= 0x140000000UL;
        return (uint)v;
    }
}

internal struct Func
{
    public uint Begin;
    public uint End;
}

internal sealed class Import
{
    public string Dll;
    public string Name;
    public uint IatRva;
}

internal sealed class Section
{
    public string Name;
    public uint VirtualAddress;
    public uint VirtualSize;
    public uint RawSize;
    public uint RawOffset;
}

internal sealed class Pe
{
    public byte[] Data;
    public ulong ImageBase;
    public List<Section> Sections = new List<Section>();
    public List<Import> Imports = new List<Import>();
    public List<Func> Functions = new List<Func>();
    private Dictionary<uint, Func> _byBegin;

    public static Pe Load(string path)
    {
        var pe = new Pe { Data = File.ReadAllBytes(path) };
        byte[] d = pe.Data;
        int peOff = BitConverter.ToInt32(d, 0x3C);
        if (BitConverter.ToUInt16(d, peOff + 24) != 0x20B) throw new InvalidOperationException("not PE32+");
        pe.ImageBase = BitConverter.ToUInt64(d, peOff + 24 + 24);
        ushort count = BitConverter.ToUInt16(d, peOff + 6);
        ushort optSize = BitConverter.ToUInt16(d, peOff + 20);
        int table = peOff + 24 + optSize;
        for (int i = 0; i < count; i++)
        {
            int o = table + i * 40;
            pe.Sections.Add(new Section
            {
                Name = Encoding.ASCII.GetString(d, o, 8).TrimEnd('\0'),
                VirtualSize = BitConverter.ToUInt32(d, o + 8),
                VirtualAddress = BitConverter.ToUInt32(d, o + 12),
                RawSize = BitConverter.ToUInt32(d, o + 16),
                RawOffset = BitConverter.ToUInt32(d, o + 20)
            });
        }
        int dirs = peOff + 24 + 112;
        uint importRva = BitConverter.ToUInt32(d, dirs + 8);
        if (importRva != 0)
        {
            int io = (int)pe.RvaToOffset(importRva);
            for (int n = 0; ; n++)
            {
                int o = io + n * 20;
                uint orig = BitConverter.ToUInt32(d, o);
                uint nameRva = BitConverter.ToUInt32(d, o + 12);
                uint first = BitConverter.ToUInt32(d, o + 16);
                if (nameRva == 0) break;
                string dll = pe.CString(nameRva, 80);
                uint thunk = orig != 0 ? orig : first;
                int to = (int)pe.RvaToOffset(thunk);
                for (int t = 0; ; t++)
                {
                    ulong e = BitConverter.ToUInt64(d, to + t * 8);
                    if (e == 0) break;
                    string fn = (e & 0x8000000000000000UL) != 0 ? "#" + (e & 0xFFFF) : pe.CString((uint)(e & 0x7FFFFFFF) + 2, 120);
                    pe.Imports.Add(new Import { Dll = dll, Name = fn, IatRva = first + (uint)(t * 8) });
                }
            }
        }
        uint pdataRva = BitConverter.ToUInt32(d, dirs + 3 * 8);
        uint pdataSize = BitConverter.ToUInt32(d, dirs + 3 * 8 + 4);
        if (pdataRva != 0)
        {
            int po = (int)pe.RvaToOffset(pdataRva);
            for (int i = 0; i < pdataSize / 12; i++)
            {
                uint b = BitConverter.ToUInt32(d, po + i * 12);
                uint e = BitConverter.ToUInt32(d, po + i * 12 + 4);
                if (b != 0 && e > b) pe.Functions.Add(new Func { Begin = b, End = e });
            }
        }
        pe.Functions.Sort((a, b) => a.Begin.CompareTo(b.Begin));
        pe._byBegin = new Dictionary<uint, Func>();
        foreach (var f in pe.Functions) pe._byBegin[f.Begin] = f;
        return pe;
    }

    public uint RvaToOffset(uint rva)
    {
        foreach (var s in Sections)
            if (rva >= s.VirtualAddress && rva < s.VirtualAddress + Math.Max(s.VirtualSize, s.RawSize))
                return s.RawOffset + (rva - s.VirtualAddress);
        throw new InvalidOperationException("rva 0x" + rva.ToString("X"));
    }

    public uint OffsetToRva(uint off)
    {
        foreach (var s in Sections)
            if (off >= s.RawOffset && off < s.RawOffset + s.RawSize)
                return s.VirtualAddress + (off - s.RawOffset);
        return off;
    }

    public string CString(uint rva, int max)
    {
        int off;
        try { off = (int)RvaToOffset(rva); } catch { return ""; }
        var sb = new StringBuilder();
        for (int i = 0; i < max && off + i < Data.Length; i++)
        {
            byte b = Data[off + i];
            if (b == 0 || b < 32 || b > 126) break;
            sb.Append((char)b);
        }
        return sb.ToString();
    }

    public string WString(uint rva, int max)
    {
        int off;
        try { off = (int)RvaToOffset(rva); } catch { return ""; }
        var sb = new StringBuilder();
        for (int i = 0; i < max && off + i * 2 + 1 < Data.Length; i++)
        {
            ushort c = BitConverter.ToUInt16(Data, off + i * 2);
            if (c == 0 || c < 32 || c > 126) break;
            sb.Append((char)c);
        }
        return sb.ToString();
    }

    public List<int> FindAll(byte[] pat)
    {
        var hits = new List<int>();
        for (int i = 0; i + pat.Length <= Data.Length; i++)
        {
            if (Data[i] != pat[0]) continue;
            int j = 1;
            while (j < pat.Length && Data[i + j] == pat[j]) j++;
            if (j == pat.Length) hits.Add(i);
        }
        return hits;
    }

    public Func FunctionOf(uint rva)
    {
        int lo = 0, hi = Functions.Count - 1, ans = -1;
        while (lo <= hi)
        {
            int mid = (lo + hi) >> 1;
            if (Functions[mid].Begin <= rva) { ans = mid; lo = mid + 1; } else hi = mid - 1;
        }
        if (ans >= 0 && rva < Functions[ans].End) return Functions[ans];
        return default;
    }

    public List<Instruction> Decode(Func f)
    {
        int off = (int)RvaToOffset(f.Begin);
        int len = (int)(f.End - f.Begin);
        var dec = Iced.Intel.Decoder.Create(64, new ByteArrayCodeReader(Data, off, len));
        dec.IP = ImageBase + f.Begin;
        var list = new List<Instruction>();
        ulong end = ImageBase + f.End;
        while (dec.IP < end)
        {
            var ins = dec.Decode();
            list.Add(ins);
        }
        return list;
    }

    public string Describe(Instruction ins)
    {
        string text = ins.ToString();
        string note = "";
        if (ins.IsIPRelativeMemoryOperand)
        {
            ulong target = ins.IPRelativeMemoryAddress;
            uint rva = (uint)(target - ImageBase);
            var imp = Imports.FirstOrDefault(i => i.IatRva == rva);
            if (imp != null) note = $" ; {imp.Dll}!{imp.Name}";
            else
            {
                string s = CString(rva, 70);
                if (s.Length >= 3) note = $" ; \"{s}\"";
                else
                {
                    string w = WString(rva, 70);
                    if (w.Length >= 3) note = $" ; L\"{w}\"";
                }
            }
        }
        else if (ins.FlowControl == FlowControl.Call && ins.Op0Kind == OpKind.NearBranch64)
        {
            uint rva = (uint)(ins.NearBranchTarget - ImageBase);
            var f = FunctionOf(rva);
            if (f.Begin == rva) note = $" ; fn size 0x{f.End - f.Begin:X}";
        }
        return $"0x{ins.IP:X} {text}{note}";
    }

    public void PrintRefs(uint rva, uint size)
    {
        int found = 0;
        foreach (var f in Functions)
        {
            List<Instruction> code = null;
            int off = (int)RvaToOffset(f.Begin);
            int len = (int)(f.End - f.Begin);
            if (!QuickScan(off, len, f.Begin, rva, size)) continue;
            code = Decode(f);
            for (int i = 0; i < code.Count; i++)
            {
                var ins = code[i];
                if (!ins.IsIPRelativeMemoryOperand) continue;
                ulong t = ins.IPRelativeMemoryAddress - ImageBase;
                if (t < rva || t >= rva + size) continue;
                found++;
                Console.WriteLine($"-- ref at 0x{ins.IP:X} in fn 0x{ImageBase + f.Begin:X} size 0x{f.End - f.Begin:X}");
                for (int k = Math.Max(0, i - 6); k <= Math.Min(code.Count - 1, i + 6); k++)
                    Console.WriteLine((k == i ? " >> " : "    ") + Describe(code[k]));
            }
        }
        Console.WriteLine($"refs: {found}");
    }

    private bool QuickScan(int off, int len, uint beginRva, uint rva, uint size)
    {
        for (int i = 0; i + 4 <= len; i++)
        {
            int disp = BitConverter.ToInt32(Data, off + i);
            for (int extra = 4; extra <= 8; extra++)
            {
                long next = (long)beginRva + i + extra;
                long t = next + disp;
                if (t >= rva && t < (long)rva + size) return true;
            }
        }
        return false;
    }

    public IEnumerable<(uint site, Func fn)> DirectCalls(uint target)
    {
        foreach (var f in Functions)
        {
            int off = (int)RvaToOffset(f.Begin);
            int len = (int)(f.End - f.Begin);
            for (int i = 0; i + 5 <= len; i++)
            {
                if (Data[off + i] != 0xE8 && Data[off + i] != 0xE9) continue;
                int rel = BitConverter.ToInt32(Data, off + i + 1);
                if (f.Begin + (uint)i + 5 + (uint)rel == target)
                    yield return (f.Begin + (uint)i, f);
            }
        }
    }
}
