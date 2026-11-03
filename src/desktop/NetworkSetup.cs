// Windows-only, fixed-purpose network helper. The viewer stays unelevated.
// No persistent settings, DHCP/DNS/gateway changes, shell commands or file writes.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Management;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

class NetworkSetup {
    const string Board = "192.168.1.10";
    [StructLayout(LayoutKind.Sequential)]
    struct Route {
        public uint dest, mask, policy, nextHop, index, type, protocol, age, nextHopAS;
        public uint metric1, metric2, metric3, metric4, metric5;
    }
    [DllImport("iphlpapi.dll")] static extern uint AddIPAddress(uint address,uint mask,uint index,out uint context,out uint instance);
    [DllImport("iphlpapi.dll")] static extern uint DeleteIPAddress(uint context);
    [DllImport("iphlpapi.dll")] static extern uint GetIpForwardTable(IntPtr table,ref uint size,bool order);
    [DllImport("iphlpapi.dll")] static extern uint CreateIpForwardEntry(ref Route row);
    [DllImport("iphlpapi.dll")] static extern uint DeleteIpForwardEntry(ref Route row);
    static uint IPv4(string s) { return BitConverter.ToUInt32(IPAddress.Parse(s).GetAddressBytes(),0); }

    static HashSet<string> PhysicalIds() {
        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using (var q = new ManagementObjectSearcher("SELECT GUID FROM Win32_NetworkAdapter WHERE PhysicalAdapter = True"))
        using (var rows = q.Get()) foreach (ManagementObject row in rows) {
            using(row) { if(row["GUID"]!=null)ids.Add(row["GUID"].ToString().Trim('{','}')); }
        }
        return ids;
    }
    static List<Route> Routes() {
        uint size=0;uint code=GetIpForwardTable(IntPtr.Zero,ref size,false);
        if(code!=122)throw new Exception("Route table query: "+code);
        if(size<4 || size>16*1024*1024)throw new Exception("Invalid route table size");
        IntPtr p=Marshal.AllocHGlobal((int)size);
        try {
            code=GetIpForwardTable(p,ref size,false);if(code!=0)throw new Exception("Route table read: "+code);
            int n=Marshal.ReadInt32(p);int rowSize=Marshal.SizeOf(typeof(Route));
            if(n<0 || 4L+n*(long)rowSize>size)throw new Exception("Invalid route table rows");
            var rows=new List<Route>();
            for(int i=0;i<n;i++)rows.Add((Route)Marshal.PtrToStructure(IntPtr.Add(p,4+i*rowSize),typeof(Route)));
            return rows;
        } finally {Marshal.FreeHGlobal(p);}
    }
    static object Inspect() {
        var physical=PhysicalIds();var items=new List<object>();
        var reserved=NetworkInterface.GetAllNetworkInterfaces().SelectMany(a=>a.GetIPProperties().UnicastAddresses)
            .Where(a=>a.Address.AddressFamily==AddressFamily.InterNetwork).Select(a=>a.Address.ToString()).Distinct().ToArray();
        foreach(var n in NetworkInterface.GetAllNetworkInterfaces()) {
            if(n.NetworkInterfaceType!=NetworkInterfaceType.Ethernet || !physical.Contains(n.Id.Trim('{','}')))continue;
            var p=n.GetIPProperties();var v4=p.GetIPv4Properties();if(v4==null)continue;
            items.Add(new {id=n.Id.Trim('{','}'),name=n.Name,description=n.Description,index=v4.Index,
                up=n.OperationalStatus==OperationalStatus.Up,physical=true,dhcp=v4.IsDhcpEnabled,reserved_ips=reserved,
                mac=n.GetPhysicalAddress().ToString(),
                ips=p.UnicastAddresses.Where(a=>a.Address.AddressFamily==AddressFamily.InterNetwork).Select(a=>new {
                    address=a.Address.ToString(),mask=a.IPv4Mask.ToString(),dad=a.DuplicateAddressDetectionState.ToString()}).ToArray(),
                gateways=p.GatewayAddresses.Where(a=>a.Address.AddressFamily==AddressFamily.InterNetwork).Select(a=>a.Address.ToString()).ToArray()});
        }
        return new {schema="stereo_network_v1",adapters=items};
    }
    static NetworkInterface Find(string id) {
        Guid g; if(!Guid.TryParse(id,out g))throw new Exception("Invalid adapter identity");
        var p=PhysicalIds();var n=NetworkInterface.GetAllNetworkInterfaces().SingleOrDefault(a=>String.Equals(a.Id.Trim('{','}'),g.ToString(),StringComparison.OrdinalIgnoreCase));
        if(n==null || n.NetworkInterfaceType!=NetworkInterfaceType.Ethernet || !p.Contains(n.Id.Trim('{','}')) || n.OperationalStatus!=OperationalStatus.Up)
            throw new Exception("所选有线网卡未连接，请检查电脑到板卡 J23 的网线。");
        return n;
    }
    static Process Parent(string pidText,string bornText) {
        int pid;long born;
        if(!Int32.TryParse(pidText,out pid)||pid<=0||!Int64.TryParse(bornText,out born))throw new Exception("Invalid parent");
        var parent=Process.GetProcessById(pid);
        if(parent.HasExited || parent.StartTime.ToUniversalTime().ToFileTimeUtc()!=born)throw new Exception("启动应用已结束。");
        string root=Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location),"..",".."));
        string expected=Path.Combine(root,"双目拍照展示.exe");
        if(!String.Equals(Path.GetFullPath(parent.MainModule.FileName),expected,StringComparison.OrdinalIgnoreCase))
            throw new Exception("请从同一完整解压目录中的双目拍照展示应用启动网络配置。");
        return parent;
    }
    static int Lease(string id,string pid,string born,string host) {
        if(host!="192.168.1.100" && host!="192.168.1.101")throw new Exception("Invalid fixed peer address");
        using(var parent=Parent(pid,born)) {
            var adapter=Find(id);uint index=(uint)adapter.GetIPProperties().GetIPv4Properties().Index;
            foreach(var n in NetworkInterface.GetAllNetworkInterfaces())
                if(n.GetIPProperties().UnicastAddresses.Any(a=>a.Address.ToString()==host))throw new Exception("临时地址已被本机使用，请重新检测网卡。");
            var initial=Routes();
            if(initial.Any(r=>r.dest==IPv4(Board)&&r.mask==0xffffffff))throw new Exception("已有到板卡地址的专用路由；保留原配置，请检查现有网络。");
            uint context=0,instance=0;bool added=false,routed=false;
            Route route=new Route();
            try {
                uint code=AddIPAddress(IPv4(host),0xffffffff,index,out context,out instance);
                if(code!=0)throw new Exception("临时地址配置失败，Windows 错误码："+code);
                added=true;bool ready=false;
                for(int i=0;i<40 && !parent.HasExited;i++) {
                    var address=Find(id).GetIPProperties().UnicastAddresses.FirstOrDefault(a=>a.Address.ToString()==host);
                    if(address!=null && address.DuplicateAddressDetectionState==DuplicateAddressDetectionState.Duplicate)
                        throw new Exception("检测到地址冲突；已撤销本次临时地址。");
                    if(address!=null && address.DuplicateAddressDetectionState==DuplicateAddressDetectionState.Preferred){ready=true;break;}
                    Thread.Sleep(250);
                }
                if(!ready)throw new Exception("临时地址尚未可用，请检查网线后重试。");
                uint metric=Routes().Where(r=>r.index==index && r.metric1<UInt32.MaxValue).Select(r=>r.metric1).DefaultIfEmpty(256u).Min();
                route=new Route {dest=IPv4(Board),mask=0xffffffff,index=index,type=3,protocol=3,metric1=metric+1,
                    metric2=UInt32.MaxValue,metric3=UInt32.MaxValue,metric4=UInt32.MaxValue,metric5=UInt32.MaxValue};
                code=CreateIpForwardEntry(ref route);
                if(code!=0)throw new Exception("板卡专用路由配置失败，Windows 错误码："+code);
                routed=true;
                // Keep the original process handle: PID reuse cannot retain the lease.
                parent.WaitForExit();
                return 0;
            } finally {
                uint routeError=0,addressError=0;
                if(routed)routeError=DeleteIpForwardEntry(ref route);
                if(added)addressError=DeleteIPAddress(context);
                // A NIC reset may have removed the temporary entries already.
                if((routeError!=0 && routeError!=1168 && routeError!=87) ||
                   (addressError!=0 && addressError!=1168 && addressError!=87))
                    MessageBox.Show("临时网络配置清理未全部完成。错误码："+routeError+" / "+addressError+
                        "。重新启用该有线网卡或重启 Windows 可清除这些非持久配置。","双目拍照 · 网络恢复");
            }
        }
    }
    [STAThread] static int Main(string[] args) {
        try {
            if(args.Length==1 && args[0]=="--inspect") {
                Console.OutputEncoding=System.Text.Encoding.UTF8;
                Console.WriteLine(new JavaScriptSerializer().Serialize(Inspect()));return 0;
            }
            if(args.Length==5 && args[0]=="--lease")return Lease(args[1],args[2],args[3],args[4]);
            throw new Exception("Unsupported helper command");
        } catch(Exception e) {
            if(args.Length>0 && args[0]=="--inspect")Console.Error.WriteLine(e.Message);
            else MessageBox.Show(e.Message,"双目拍照 · 网络连接",MessageBoxButtons.OK,MessageBoxIcon.Information);
            return 2;
        }
    }
}
