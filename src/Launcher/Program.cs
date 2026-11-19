using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class Program
{
    private const uint ProcessCreateThread = 0x0002;
    private const uint ProcessQueryInformation = 0x0400;
    private const uint ProcessVmOperation = 0x0008;
    private const uint ProcessVmWrite = 0x0020;
    private const uint ProcessVmRead = 0x0010;
    private const uint MemCommit = 0x1000;
    private const uint MemReserve = 0x2000;
    private const uint PageReadWrite = 0x04;

    private sealed class Game
    {
        public string Id;
        public string Title;
        public string Dir;
        public string Exe;
        public string AppId;
        public uint Timestamp;
        public uint SizeOfImage;
    }

    public static int Main(string[] args)
    {
        string baseDir = AppContext.BaseDirectory;
        var games = new Dictionary<string, Game>
        {
            ["kof2002"] = new Game
            {
                Id = "kof2002", Title = "KOF 2002 Unlimited Match",
                Dir = @"D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match",
                Exe = "KingOfFighters2002UM_x64.exe", AppId = "222440", Timestamp = 0x6243D069, SizeOfImage = 0x41AE000
            },
            ["kof98"] = new Game
            {
                Id = "kof98", Title = "KOF '98 Ultimate Match Final Edition",
                Dir = @"D:\SteamLibrary\steamapps\common\The King of Fighters'98 Ultimate Match",
                Exe = "KingOfFighters98UM_x64.exe", AppId = "222420", Timestamp = 0x62033053, SizeOfImage = 0x1B31000
            },
            ["kof13"] = new Game
            {
                Id = "kof13", Title = "THE KING OF FIGHTERS XIII",
                Dir = @"D:\SteamLibrary\steamapps\common\THE KING OF FIGHTERS XIII GLOBAL MATCH",
                Exe = "game.exe", AppId = "3050220", Timestamp = 0x686CE4A0, SizeOfImage = 0xA5E000
            },
        };
        string selected = "kof2002";
        string cfg = Path.Combine(baseDir, "launcher.cfg");
        if (File.Exists(cfg)) ReadLauncherCfg(cfg, games, ref selected);

        bool injectOnly = false;
        foreach (string arg in args)
        {
            string a = arg.ToLowerInvariant();
            if (a == "--inject-only") injectOnly = true;
            else if (games.ContainsKey(a)) selected = a;
            else
            {
                Console.WriteLine("Uso: KofVsLauncher [kof2002|kof98|kof13] [--inject-only]");
                return 1;
            }
        }

        Game game = games[selected];
        if (game.Id == "kof13" && IntPtr.Size != 4)
        {
            Console.WriteLine("KOF XIII es de 32 bits. Usa Jugar KOF13 con selector.bat.");
            return 1;
        }
        string dll = Path.Combine(baseDir, "KofVsSelector.dll");
        if (!File.Exists(dll))
        {
            Console.WriteLine("Falta KofVsSelector.dll junto al lanzador.");
            return 1;
        }
        if (!File.Exists(Path.Combine(baseDir, "kofvs.ini")))
            Console.WriteLine("Aviso: no hay kofvs.ini; la DLL usará los valores por defecto.");

        string exePath = Path.Combine(game.Dir, game.Exe);
        if (!File.Exists(exePath))
        {
            Console.WriteLine("No está el ejecutable del juego: " + exePath);
            return 1;
        }
        if (!CheckVersion(exePath, game))
        {
            Console.WriteLine("La versión del ejecutable no coincide con la analizada. No se inyecta el mod.");
            return 1;
        }

        Console.WriteLine("Juego: " + game.Title + " (" + game.Exe + ")");
        Environment.SetEnvironmentVariable("SteamAppId", game.AppId);
        Environment.SetEnvironmentVariable("SteamGameId", game.AppId);
        string music = MusicDll(baseDir, game.Id);

        Process existing = FindGame(game.Exe);
        if (existing != null)
        {
            if (ModuleLoaded(existing, "KofVsSelector.dll"))
            {
                Console.WriteLine("El mod ya está cargado en el proceso " + existing.Id + ".");
                if (music != null && !ModuleLoaded(existing, Path.GetFileName(music))) InjectMusic(existing.Id, music);
                return 0;
            }
            Console.WriteLine("El juego ya está abierto. Inyecto el mod en el proceso " + existing.Id + ".");
            if (!Inject(existing.Id, dll)) return 1;
            if (music != null && !ModuleLoaded(existing, Path.GetFileName(music))) InjectMusic(existing.Id, music);
            return Done(baseDir);
        }
        if (injectOnly)
        {
            Console.WriteLine("El juego no está en ejecución.");
            return 1;
        }
        return StartAndInject(exePath, game.Dir, dll, music) ? Done(baseDir) : 1;
    }

    // The music selector of the same game is optional: it is loaded only when its DLL sits next to the launcher.
    private static string MusicDll(string baseDir, string id)
    {
        string name = id == "kof98" ? "Kof98Bgm.dll" : id == "kof2002" ? "Kof2002Bgm.dll" : null;
        if (name == null) return null;
        string path = Path.Combine(baseDir, name);
        return File.Exists(path) ? path : null;
    }

    private static void InjectMusic(int pid, string music)
    {
        if (Inject(pid, music)) Console.WriteLine("Selector de música activo: " + Path.GetFileName(music));
        else Console.WriteLine("Aviso: no se cargó " + Path.GetFileName(music) + ". El selector de controles sigue activo.");
    }

    private static int Done(string baseDir)
    {
        Console.WriteLine("Mod activo solo para esta sesión. Para jugar sin el mod, cierra el juego y ábrelo desde Steam.");
        Console.WriteLine("Registro: " + Path.Combine(baseDir, "kofvs.log"));
        return 0;
    }

    private static bool CheckVersion(string exePath, Game game)
    {
        using var fs = File.OpenRead(exePath);
        var br = new BinaryReader(fs);
        fs.Position = 0x3C;
        int pe = br.ReadInt32();
        fs.Position = pe + 8;
        uint ts = br.ReadUInt32();
        fs.Position = pe + 24 + 56;
        uint size = br.ReadUInt32();
        Console.WriteLine($"Versión del ejecutable: timestamp 0x{ts:X8}, SizeOfImage 0x{size:X}");
        return ts == game.Timestamp && size == game.SizeOfImage;
    }

    private static void ReadLauncherCfg(string path, Dictionary<string, Game> games, ref string selected)
    {
        foreach (string raw in File.ReadAllLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] == '#' || line[0] == ';') continue;
            int eq = line.IndexOf('=');
            if (eq <= 0) continue;
            string key = line.Substring(0, eq).Trim().ToLowerInvariant();
            string value = line.Substring(eq + 1).Trim();
            if (key == "game" && games.ContainsKey(value.ToLowerInvariant())) selected = value.ToLowerInvariant();
            foreach (var g in games.Values)
            {
                if (key == g.Id + "_dir") g.Dir = value;
                else if (key == g.Id + "_exe") g.Exe = value;
                else if (key == g.Id + "_appid") g.AppId = value;
            }
        }
    }

    private static Process FindGame(string exe)
    {
        Process[] list = Process.GetProcessesByName(Path.GetFileNameWithoutExtension(exe));
        return list.Length == 0 ? null : list[0];
    }

    private static bool ModuleLoaded(Process p, string name)
    {
        try
        {
            foreach (ProcessModule m in p.Modules)
                if (string.Equals(m.ModuleName, name, StringComparison.OrdinalIgnoreCase)) return true;
        }
        catch
        {
        }
        return false;
    }

    private static bool StartAndInject(string exePath, string gameDir, string dll, string music)
    {
        var si = new STARTUPINFOW { cb = System.Runtime.InteropServices.Marshal.SizeOf<STARTUPINFOW>() };
        if (!CreateProcessW(exePath, null, System.IntPtr.Zero, System.IntPtr.Zero, false, CREATE_SUSPENDED, System.IntPtr.Zero, gameDir, ref si, out PROCESS_INFORMATION pi))
        {
            Console.WriteLine("No se pudo iniciar el juego. Error " + Marshal.GetLastWin32Error() + ".");
            return false;
        }
        Console.WriteLine("Proceso " + pi.dwProcessId + " en pausa. Inyecto antes de que abra la pantalla.");
        bool injected = Inject(pi.dwProcessId, dll);
        if (injected && music != null) InjectMusic(pi.dwProcessId, music);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (!injected)
        {
            Console.WriteLine("La inyección en pausa falló.");
            return false;
        }
        System.Threading.Thread.Sleep(1500);
        try
        {
            if (!Process.GetProcessById(pi.dwProcessId).HasExited) return true;
        }
        catch
        {
        }
        Console.WriteLine("El primer proceso terminó. Busco el relanzamiento de Steam.");
        string name = Path.GetFileNameWithoutExtension(exePath);
        for (int i = 0; i < 40; i++)
        {
            foreach (Process candidate in Process.GetProcessesByName(name))
            {
                if (candidate.Id == pi.dwProcessId) continue;
                Console.WriteLine("Inyecto en el proceso de Steam " + candidate.Id + ".");
                if (!Inject(candidate.Id, dll)) return false;
                if (music != null) InjectMusic(candidate.Id, music);
                return true;
            }
            System.Threading.Thread.Sleep(200);
        }
        Console.WriteLine("No apareció un proceso nuevo del juego.");
        return false;
    }

    private static bool Inject(int pid, string dll)
    {
        IntPtr process = OpenProcess(ProcessCreateThread | ProcessQueryInformation | ProcessVmOperation | ProcessVmWrite | ProcessVmRead, false, pid);
        if (process == IntPtr.Zero)
        {
            Console.WriteLine("OpenProcess falló con el error " + Marshal.GetLastWin32Error() + ".");
            return false;
        }
        try
        {
            byte[] data = Encoding.Unicode.GetBytes(dll + "\0");
            IntPtr remote = VirtualAllocEx(process, IntPtr.Zero, (UIntPtr)data.Length, MemCommit | MemReserve, PageReadWrite);
            if (remote == IntPtr.Zero || !WriteProcessMemory(process, remote, data, (UIntPtr)data.Length, out _))
            {
                Console.WriteLine("No se pudo escribir la ruta de la DLL en el proceso (error " + Marshal.GetLastWin32Error() + ").");
                return false;
            }
            IntPtr load = GetProcAddress(GetModuleHandleW("kernel32.dll"), "LoadLibraryW");
            IntPtr thread = CreateRemoteThread(process, IntPtr.Zero, UIntPtr.Zero, load, remote, 0, IntPtr.Zero);
            if (thread == IntPtr.Zero)
            {
                Console.WriteLine("CreateRemoteThread falló con el error " + Marshal.GetLastWin32Error() + ".");
                return false;
            }
            WaitForSingleObject(thread, 10000);
            GetExitCodeThread(thread, out uint code);
            CloseHandle(thread);
            if (code == 0)
            {
                Console.WriteLine("LoadLibraryW no cargó la DLL en el juego.");
                return false;
            }
            return true;
        }
        finally
        {
            CloseHandle(process);
        }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr addr, UIntPtr size, uint type, uint protect);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteProcessMemory(IntPtr process, IntPtr addr, byte[] buffer, UIntPtr size, out UIntPtr written);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandleW(string name);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attr, UIntPtr stack, IntPtr start, IntPtr param, uint flags, IntPtr tid);

    [DllImport("kernel32.dll")]
    private static extern uint WaitForSingleObject(IntPtr handle, uint ms);

    [DllImport("kernel32.dll")]
    private static extern bool GetExitCodeThread(IntPtr thread, out uint code);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);

    private const uint CREATE_SUSPENDED = 0x00000004;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFOW
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public int dwProcessId;
        public int dwThreadId;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string dir, ref STARTUPINFOW si, out PROCESS_INFORMATION pi);

    [DllImport("kernel32.dll")]
    private static extern uint ResumeThread(IntPtr thread);
}
