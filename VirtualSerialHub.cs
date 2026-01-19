using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Ports;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

class VirtualSerialHub
{
    static List<Bridge> _bridges = new List<Bridge>();
    static int _nextId = 1;
    static bool _running = true;
    static bool _hexMode = false;

    struct SerialConfig
    {
        public string PortName;
        public int BaudRate;
        public int DataBits;
        public Parity Parity;
        public StopBits StopBits;

        public static SerialConfig Parse(string input)
        {
            var cfg = new SerialConfig { BaudRate = 9600, DataBits = 8, Parity = Parity.None, StopBits = StopBits.One };

            int colonIdx = input.IndexOf(':');
            if (colonIdx < 0)
            {
                cfg.PortName = input;
                return cfg;
            }

            cfg.PortName = input.Substring(0, colonIdx);
            string[] parts = input.Substring(colonIdx + 1).Split(',');

            if (parts.Length >= 1 && !string.IsNullOrEmpty(parts[0]))
                cfg.BaudRate = int.Parse(parts[0]);

            if (parts.Length >= 2 && !string.IsNullOrEmpty(parts[1]))
                cfg.DataBits = int.Parse(parts[1]);

            if (parts.Length >= 3 && !string.IsNullOrEmpty(parts[2]))
            {
                switch (parts[2].ToUpper())
                {
                    case "N": cfg.Parity = Parity.None; break;
                    case "O": cfg.Parity = Parity.Odd; break;
                    case "E": cfg.Parity = Parity.Even; break;
                    case "M": cfg.Parity = Parity.Mark; break;
                    case "S": cfg.Parity = Parity.Space; break;
                    default: cfg.Parity = Parity.None; break;
                }
            }

            if (parts.Length >= 4 && !string.IsNullOrEmpty(parts[3]))
            {
                switch (parts[3])
                {
                    case "1": cfg.StopBits = StopBits.One; break;
                    case "1.5": cfg.StopBits = StopBits.OnePointFive; break;
                    case "2": cfg.StopBits = StopBits.Two; break;
                    default: cfg.StopBits = StopBits.One; break;
                }
            }

            return cfg;
        }

        public override string ToString()
        {
            char p = 'N';
            switch (Parity)
            {
                case Parity.Odd: p = 'O'; break;
                case Parity.Even: p = 'E'; break;
                case Parity.Mark: p = 'M'; break;
                case Parity.Space: p = 'S'; break;
            }
            string s = StopBits == StopBits.OnePointFive ? "1.5" : (StopBits == StopBits.Two ? "2" : "1");
            return string.Format("{0}:{1},{2},{3},{4}", PortName, BaudRate, DataBits, p, s);
        }

        public string Short { get { return PortName; } }
    }

    static void Main(string[] args)
    {
        List<string> argList = new List<string>(args);

        if (argList.Contains("--hex") || argList.Contains("-h"))
        {
            _hexMode = true;
            argList.Remove("--hex");
            argList.Remove("-h");
        }

        args = argList.ToArray();

        Console.WriteLine("VirtualSerialHub v1.2 - User-Mode Serial Bridge");
        Console.WriteLine("================================================");
        if (_hexMode) Console.WriteLine("[Hex dump mode enabled]");
        Console.WriteLine();

        if (args.Length == 0) { Interactive(); return; }

        switch (args[0].ToLower())
        {
            case "bridge":
                if (args.Length < 3) { Console.WriteLine("Usage: bridge <COM1[:settings]> <COM2[:settings]>"); return; }
                StartBridge(args[1], args[2]);
                WaitForExit();
                break;
            case "loopback":
                int port = args.Length > 1 ? int.Parse(args[1]) : 9600;
                StartLoopback(port);
                WaitForExit();
                break;
            case "tcpserial":
                if (args.Length < 3) { Console.WriteLine("Usage: tcpserial <COMx[:settings]> <TCPPort>"); return; }
                StartTcpSerial(args[1], int.Parse(args[2]));
                WaitForExit();
                break;
            case "list":
                ListPorts();
                break;
            default:
                PrintHelp();
                break;
        }
    }

    static void Interactive()
    {
        PrintHelp();
        while (_running)
        {
            Console.Write("\n> ");
            string line = Console.ReadLine();
            if (string.IsNullOrWhiteSpace(line)) continue;

            string[] parts = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
            string cmd = parts[0].ToLower();

            try
            {
                switch (cmd)
                {
                    case "bridge":
                        if (parts.Length < 3) Console.WriteLine("Usage: bridge <COM1[:settings]> <COM2[:settings]>");
                        else StartBridge(parts[1], parts[2]);
                        break;
                    case "loopback":
                        StartLoopback(parts.Length > 1 ? int.Parse(parts[1]) : 9600);
                        break;
                    case "tcpserial":
                        if (parts.Length < 3) Console.WriteLine("Usage: tcpserial <COMx[:settings]> <TCPPort>");
                        else StartTcpSerial(parts[1], int.Parse(parts[2]));
                        break;
                    case "list":
                        ListPorts();
                        break;
                    case "status":
                        ShowStatus();
                        break;
                    case "stop":
                        if (parts.Length < 2) Console.WriteLine("Usage: stop <id>");
                        else StopBridge(int.Parse(parts[1]));
                        break;
                    case "hex":
                        _hexMode = !_hexMode;
                        Console.WriteLine("Hex mode: " + (_hexMode ? "ON" : "OFF"));
                        break;
                    case "quit":
                    case "exit":
                        _running = false;
                        StopAll();
                        break;
                    case "help":
                        PrintHelp();
                        break;
                    default:
                        Console.WriteLine("Unknown command. Type 'help' for options.");
                        break;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("Error: " + ex.Message);
            }
        }
    }

    static void PrintHelp()
    {
        Console.WriteLine("Commands:");
        Console.WriteLine("  bridge <COM1> <COM2>    - Bridge two serial ports");
        Console.WriteLine("  loopback [port]         - TCP loopback echo server (default: 9600)");
        Console.WriteLine("  tcpserial <COMx> <port> - Bridge serial port to TCP");
        Console.WriteLine("  list                    - Show available COM ports");
        Console.WriteLine("  status                  - Show active bridges");
        Console.WriteLine("  stop <id>               - Stop a bridge");
        Console.WriteLine("  hex                     - Toggle hex dump mode");
        Console.WriteLine("  quit                    - Exit");
        Console.WriteLine("\nPort format: COMx[:Baud,DataBits,Parity,StopBits]");
        Console.WriteLine("  Examples:  COM3              (9600,8,N,1 default)");
        Console.WriteLine("             COM3:115200       (115200,8,N,1)");
        Console.WriteLine("             COM3:9600,7,E,2   (9600,7,Even,2)");
        Console.WriteLine("  Parity:    N=None O=Odd E=Even M=Mark S=Space");
        Console.WriteLine("  StopBits:  1, 1.5, 2");
        Console.WriteLine("\nFlags: --hex, -h          - Enable hex dump at startup");
    }

    static void ListPorts()
    {
        Console.WriteLine("\nAvailable COM Ports:");
        Console.WriteLine("--------------------");
        string[] ports = SerialPort.GetPortNames();
        if (ports.Length == 0)
            Console.WriteLine("  (none found)");
        else
            foreach (string p in ports)
                Console.WriteLine("  " + p);
    }

    static void ShowStatus()
    {
        Console.WriteLine("\nActive Bridges:");
        Console.WriteLine("---------------------------------------------------------------------------------------");
        Console.WriteLine("  ID | Type       | Endpoints                                    | Rx       | Tx       ");
        Console.WriteLine("---------------------------------------------------------------------------------------");
        lock (_bridges)
        {
            if (_bridges.Count == 0)
                Console.WriteLine("  (none)");
            else
                foreach (var b in _bridges)
                    Console.WriteLine(b.StatusLine());
        }
        Console.WriteLine("---------------------------------------------------------------------------------------");
        Console.WriteLine("Hex mode: " + (_hexMode ? "ON" : "OFF"));
    }

    static void StartBridge(string port1, string port2)
    {
        var cfg1 = SerialConfig.Parse(port1);
        var cfg2 = SerialConfig.Parse(port2);
        var b = new SerialBridge(_nextId++, cfg1, cfg2);
        lock (_bridges) _bridges.Add(b);
        b.Start();
        Console.WriteLine("Started bridge #{0}: {1} <-> {2}", b.Id, cfg1, cfg2);
    }

    static void StartLoopback(int port)
    {
        var b = new TcpLoopback(_nextId++, port);
        lock (_bridges) _bridges.Add(b);
        b.Start();
        Console.WriteLine("Started loopback #{0} on TCP port {1}", b.Id, port);
        Console.WriteLine("  Connect two apps to 127.0.0.1:{0} - data will be echoed between them", port);
    }

    static void StartTcpSerial(string comPort, int tcpPort)
    {
        var cfg = SerialConfig.Parse(comPort);
        var b = new TcpSerialBridge(_nextId++, cfg, tcpPort);
        lock (_bridges) _bridges.Add(b);
        b.Start();
        Console.WriteLine("Started TCP-Serial bridge #{0}: {1} <-> TCP:{2}", b.Id, cfg, tcpPort);
    }

    static void StopBridge(int id)
    {
        Bridge b = null;
        lock (_bridges)
        {
            b = _bridges.Find(x => x.Id == id);
            if (b != null) _bridges.Remove(b);
        }
        if (b != null) { b.Stop(); Console.WriteLine("Stopped bridge #{0}", id); }
        else Console.WriteLine("Bridge #{0} not found", id);
    }

    static void StopAll()
    {
        lock (_bridges) { foreach (var b in _bridges) b.Stop(); _bridges.Clear(); }
    }

    static void WaitForExit()
    {
        Console.WriteLine("\nPress Ctrl+C or Enter to stop...");
        Console.CancelKeyPress += (s, e) => { e.Cancel = true; _running = false; };
        while (_running && Console.KeyAvailable == false) Thread.Sleep(100);
        StopAll();
    }

    static void PrintHex(string prefix, byte[] data, int length, ConsoleColor color)
    {
        if (!_hexMode || length == 0) return;

        StringBuilder sb = new StringBuilder();
        sb.Append(prefix);
        sb.Append(" ");

        for (int i = 0; i < length; i++)
        {
            sb.Append(data[i].ToString("X2"));
            if (i < length - 1) sb.Append(" ");
        }

        sb.Append("  |");
        for (int i = 0; i < length; i++)
        {
            char c = (char)data[i];
            sb.Append((c >= 32 && c < 127) ? c : '.');
        }
        sb.Append("|");

        lock (Console.Out)
        {
            ConsoleColor prev = Console.ForegroundColor;
            Console.ForegroundColor = color;
            Console.WriteLine(sb.ToString());
            Console.ForegroundColor = prev;
        }
    }

    abstract class Bridge
    {
        public int Id;
        public long RxBytes, TxBytes;
        public abstract void Start();
        public abstract void Stop();
        public abstract string StatusLine();
    }

    class SerialBridge : Bridge
    {
        SerialConfig _cfg1, _cfg2;
        SerialPort _sp1, _sp2;
        Thread _t1, _t2;
        volatile bool _active;

        public SerialBridge(int id, SerialConfig cfg1, SerialConfig cfg2)
        {
            Id = id; _cfg1 = cfg1; _cfg2 = cfg2;
        }

        public override void Start()
        {
            _sp1 = OpenPort(_cfg1);
            _sp2 = OpenPort(_cfg2);
            _active = true;
            _t1 = new Thread(() => Relay(_sp1, _sp2, _cfg1.Short, ref RxBytes, ConsoleColor.Green)) { IsBackground = true };
            _t2 = new Thread(() => Relay(_sp2, _sp1, _cfg2.Short, ref TxBytes, ConsoleColor.Yellow)) { IsBackground = true };
            _t1.Start(); _t2.Start();
        }

        SerialPort OpenPort(SerialConfig cfg)
        {
            var sp = new SerialPort(cfg.PortName, cfg.BaudRate, cfg.Parity, cfg.DataBits, cfg.StopBits);
            sp.ReadTimeout = 100;
            sp.Open();
            return sp;
        }

        void Relay(SerialPort src, SerialPort dst, string srcName, ref long counter, ConsoleColor color)
        {
            byte[] buf = new byte[4096];
            while (_active)
            {
                try
                {
                    int n = src.BaseStream.Read(buf, 0, buf.Length);
                    if (n > 0)
                    {
                        PrintHex("[" + srcName + "]", buf, n, color);
                        dst.BaseStream.Write(buf, 0, n);
                        counter += n;
                    }
                }
                catch (TimeoutException) { }
                catch { if (_active) Thread.Sleep(10); }
            }
        }

        public override void Stop()
        {
            _active = false;
            try { if (_sp1 != null) _sp1.Close(); } catch { }
            try { if (_sp2 != null) _sp2.Close(); } catch { }
        }

        public override string StatusLine()
        {
            return string.Format("  {0,2} | Serial     | {1,-20} <-> {2,-20} | {3,8} | {4,8}",
                Id, _cfg1, _cfg2, RxBytes, TxBytes);
        }
    }

    class TcpLoopback : Bridge
    {
        int _port;
        TcpListener _listener;
        List<TcpClient> _clients = new List<TcpClient>();
        Dictionary<TcpClient, int> _clientIds = new Dictionary<TcpClient, int>();
        int _nextClientId = 1;
        Thread _acceptThread;
        volatile bool _active;

        public TcpLoopback(int id, int port) { Id = id; _port = port; }

        public override void Start()
        {
            _listener = new TcpListener(IPAddress.Loopback, _port);
            _listener.Start();
            _active = true;
            _acceptThread = new Thread(AcceptLoop) { IsBackground = true };
            _acceptThread.Start();
        }

        void AcceptLoop()
        {
            while (_active)
            {
                try
                {
                    if (!_listener.Pending()) { Thread.Sleep(50); continue; }
                    var client = _listener.AcceptTcpClient();
                    int clientId;
                    lock (_clients)
                    {
                        _clients.Add(client);
                        clientId = _nextClientId++;
                        _clientIds[client] = clientId;
                    }
                    new Thread(() => ClientLoop(client, clientId)) { IsBackground = true }.Start();
                }
                catch { if (_active) Thread.Sleep(10); }
            }
        }

        void ClientLoop(TcpClient client, int clientId)
        {
            byte[] buf = new byte[4096];
            var stream = client.GetStream();
            stream.ReadTimeout = 100;

            while (_active && client.Connected)
            {
                try
                {
                    int n = stream.Read(buf, 0, buf.Length);
                    if (n == 0) break;
                    RxBytes += n;

                    PrintHex("[Cli" + clientId + " Rx]", buf, n, ConsoleColor.Green);

                    lock (_clients)
                    {
                        foreach (var c in _clients)
                        {
                            if (c != client && c.Connected)
                            {
                                try
                                {
                                    c.GetStream().Write(buf, 0, n);
                                    TxBytes += n;
                                    int targetId;
                                    if (_clientIds.TryGetValue(c, out targetId))
                                        PrintHex("[Cli" + targetId + " Tx]", buf, n, ConsoleColor.Yellow);
                                }
                                catch { }
                            }
                        }
                    }
                }
                catch (IOException) { }
                catch { Thread.Sleep(10); }
            }

            lock (_clients)
            {
                _clients.Remove(client);
                _clientIds.Remove(client);
            }
            try { client.Close(); } catch { }
        }

        public override void Stop()
        {
            _active = false;
            try { if (_listener != null) _listener.Stop(); } catch { }
            lock (_clients)
            {
                foreach (var c in _clients) try { c.Close(); } catch { }
                _clients.Clear();
                _clientIds.Clear();
            }
        }

        public override string StatusLine()
        {
            int cnt;
            lock (_clients) cnt = _clients.Count;
            return string.Format("  {0,2} | Loopback   | TCP:{1,-5} ({2} clients)                      | {3,8} | {4,8}",
                Id, _port, cnt, RxBytes, TxBytes);
        }
    }

    class TcpSerialBridge : Bridge
    {
        SerialConfig _cfg;
        int _tcpPort;
        SerialPort _serial;
        TcpListener _listener;
        List<TcpClient> _clients = new List<TcpClient>();
        Thread _acceptThread, _serialThread;
        volatile bool _active;

        public TcpSerialBridge(int id, SerialConfig cfg, int tcp) { Id = id; _cfg = cfg; _tcpPort = tcp; }

        public override void Start()
        {
            _serial = new SerialPort(_cfg.PortName, _cfg.BaudRate, _cfg.Parity, _cfg.DataBits, _cfg.StopBits);
            _serial.ReadTimeout = 100;
            _serial.Open();

            _listener = new TcpListener(IPAddress.Any, _tcpPort);
            _listener.Start();
            _active = true;

            _acceptThread = new Thread(AcceptLoop) { IsBackground = true };
            _serialThread = new Thread(SerialReadLoop) { IsBackground = true };
            _acceptThread.Start();
            _serialThread.Start();
        }

        void AcceptLoop()
        {
            while (_active)
            {
                try
                {
                    if (!_listener.Pending()) { Thread.Sleep(50); continue; }
                    var client = _listener.AcceptTcpClient();
                    lock (_clients) _clients.Add(client);
                    new Thread(() => TcpReadLoop(client)) { IsBackground = true }.Start();
                }
                catch { if (_active) Thread.Sleep(10); }
            }
        }

        void TcpReadLoop(TcpClient client)
        {
            byte[] buf = new byte[4096];
            var stream = client.GetStream();
            stream.ReadTimeout = 100;

            while (_active && client.Connected)
            {
                try
                {
                    int n = stream.Read(buf, 0, buf.Length);
                    if (n == 0) break;
                    RxBytes += n;

                    PrintHex("[TCP Rx]", buf, n, ConsoleColor.Green);

                    lock (_serial) _serial.BaseStream.Write(buf, 0, n);
                }
                catch (IOException) { }
                catch { Thread.Sleep(10); }
            }

            lock (_clients) _clients.Remove(client);
            try { client.Close(); } catch { }
        }

        void SerialReadLoop()
        {
            byte[] buf = new byte[4096];
            while (_active)
            {
                try
                {
                    int n;
                    lock (_serial) n = _serial.BaseStream.Read(buf, 0, buf.Length);
                    if (n > 0)
                    {
                        TxBytes += n;

                        PrintHex("[" + _cfg.Short + " Rx]", buf, n, ConsoleColor.Cyan);

                        lock (_clients)
                        {
                            foreach (var c in _clients)
                            {
                                if (c.Connected)
                                {
                                    try
                                    {
                                        c.GetStream().Write(buf, 0, n);
                                        PrintHex("[TCP Tx]", buf, n, ConsoleColor.Yellow);
                                    }
                                    catch { }
                                }
                            }
                        }
                    }
                }
                catch (TimeoutException) { }
                catch { if (_active) Thread.Sleep(10); }
            }
        }

        public override void Stop()
        {
            _active = false;
            try { if (_listener != null) _listener.Stop(); } catch { }
            try { if (_serial != null) _serial.Close(); } catch { }
            lock (_clients) { foreach (var c in _clients) try { c.Close(); } catch { } _clients.Clear(); }
        }

        public override string StatusLine()
        {
            int cnt;
            lock (_clients) cnt = _clients.Count;
            return string.Format("  {0,2} | TCP-Serial | {1,-20} <-> TCP:{2,-5} ({3}cli)   | {4,8} | {5,8}",
                Id, _cfg, _tcpPort, cnt, RxBytes, TxBytes);
        }
    }
}