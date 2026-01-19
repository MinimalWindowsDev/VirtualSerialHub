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

    static void Main(string[] args)
    {
        Console.WriteLine("VirtualSerialHub v1.0 - User-Mode Serial Bridge");
        Console.WriteLine("================================================\n");

        if (args.Length == 0) { Interactive(); return; }

        switch (args[0].ToLower())
        {
            case "bridge":
                if (args.Length < 3) { Console.WriteLine("Usage: bridge <COM1> <COM2>"); return; }
                StartBridge(args[1], args[2]);
                WaitForExit();
                break;
            case "loopback":
                int port = args.Length > 1 ? int.Parse(args[1]) : 9600;
                StartLoopback(port);
                WaitForExit();
                break;
            case "tcpserial":
                if (args.Length < 3) { Console.WriteLine("Usage: tcpserial <COMx> <TCPPort>"); return; }
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
                        if (parts.Length < 3) Console.WriteLine("Usage: bridge <COM1> <COM2>");
                        else StartBridge(parts[1], parts[2]);
                        break;
                    case "loopback":
                        StartLoopback(parts.Length > 1 ? int.Parse(parts[1]) : 9600);
                        break;
                    case "tcpserial":
                        if (parts.Length < 3) Console.WriteLine("Usage: tcpserial <COMx> <TCPPort>");
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
        Console.WriteLine("  quit                    - Exit");
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
        Console.WriteLine("-------------------------------------------------------------------------------");
        Console.WriteLine("  ID | Type       | Endpoints                          | Rx       | Tx       ");
        Console.WriteLine("-------------------------------------------------------------------------------");
        lock (_bridges)
        {
            if (_bridges.Count == 0)
                Console.WriteLine("  (none)");
            else
                foreach (var b in _bridges)
                    Console.WriteLine(b.StatusLine());
        }
        Console.WriteLine("-------------------------------------------------------------------------------");
    }

    static void StartBridge(string port1, string port2)
    {
        var b = new SerialBridge(_nextId++, port1, port2);
        lock (_bridges) _bridges.Add(b);
        b.Start();
        Console.WriteLine("Started bridge #{0}: {1} <-> {2}", b.Id, port1, port2);
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
        var b = new TcpSerialBridge(_nextId++, comPort, tcpPort);
        lock (_bridges) _bridges.Add(b);
        b.Start();
        Console.WriteLine("Started TCP-Serial bridge #{0}: {1} <-> TCP:{2}", b.Id, comPort, tcpPort);
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
        string _port1, _port2;
        SerialPort _sp1, _sp2;
        Thread _t1, _t2;
        volatile bool _active;

        public SerialBridge(int id, string p1, string p2) { Id = id; _port1 = p1; _port2 = p2; }

        public override void Start()
        {
            _sp1 = OpenPort(_port1);
            _sp2 = OpenPort(_port2);
            _active = true;
            _t1 = new Thread(() => Relay(_sp1, _sp2, ref RxBytes)) { IsBackground = true };
            _t2 = new Thread(() => Relay(_sp2, _sp1, ref TxBytes)) { IsBackground = true };
            _t1.Start(); _t2.Start();
        }

        SerialPort OpenPort(string name)
        {
            var sp = new SerialPort(name, 9600, Parity.None, 8, StopBits.One);
            sp.ReadTimeout = 100;
            sp.Open();
            return sp;
        }

        void Relay(SerialPort src, SerialPort dst, ref long counter)
        {
            byte[] buf = new byte[4096];
            while (_active)
            {
                try
                {
                    int n = src.BaseStream.Read(buf, 0, buf.Length);
                    if (n > 0) { dst.BaseStream.Write(buf, 0, n); counter += n; }
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
            return string.Format("  {0,2} | Serial     | {1,-10} <-> {2,-10}         | {3,8} | {4,8}",
                Id, _port1, _port2, RxBytes, TxBytes);
        }
    }

    class TcpLoopback : Bridge
    {
        int _port;
        TcpListener _listener;
        List<TcpClient> _clients = new List<TcpClient>();
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
                    lock (_clients) _clients.Add(client);
                    new Thread(() => ClientLoop(client)) { IsBackground = true }.Start();
                }
                catch { if (_active) Thread.Sleep(10); }
            }
        }

        void ClientLoop(TcpClient client)
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

                    lock (_clients)
                    {
                        foreach (var c in _clients)
                        {
                            if (c != client && c.Connected)
                            {
                                try { c.GetStream().Write(buf, 0, n); TxBytes += n; }
                                catch { }
                            }
                        }
                    }
                }
                catch (IOException) { }
                catch { Thread.Sleep(10); }
            }

            lock (_clients) _clients.Remove(client);
            try { client.Close(); } catch { }
        }

        public override void Stop()
        {
            _active = false;
            try { if (_listener != null) _listener.Stop(); } catch { }
            lock (_clients) { foreach (var c in _clients) try { c.Close(); } catch { } _clients.Clear(); }
        }

        public override string StatusLine()
        {
            int cnt;
            lock (_clients) cnt = _clients.Count;
            return string.Format("  {0,2} | Loopback   | TCP:{1,-5} ({2} clients)            | {3,8} | {4,8}",
                Id, _port, cnt, RxBytes, TxBytes);
        }
    }

    class TcpSerialBridge : Bridge
    {
        string _comPort;
        int _tcpPort;
        SerialPort _serial;
        TcpListener _listener;
        List<TcpClient> _clients = new List<TcpClient>();
        Thread _acceptThread, _serialThread;
        volatile bool _active;

        public TcpSerialBridge(int id, string com, int tcp) { Id = id; _comPort = com; _tcpPort = tcp; }

        public override void Start()
        {
            _serial = new SerialPort(_comPort, 9600, Parity.None, 8, StopBits.One);
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
                        lock (_clients)
                        {
                            foreach (var c in _clients)
                                if (c.Connected)
                                    try { c.GetStream().Write(buf, 0, n); } catch { }
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
            return string.Format("  {0,2} | TCP-Serial | {1,-10} <-> TCP:{2,-5} ({3}cli)  | {4,8} | {5,8}",
                Id, _comPort, _tcpPort, cnt, RxBytes, TxBytes);
        }
    }
}