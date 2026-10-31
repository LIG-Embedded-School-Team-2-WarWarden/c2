using System.Buffers.Binary;
using System.IO.MemoryMappedFiles;
using System.IO.Pipes;
using System.Text.Json;

// Independent IPC test client. No GUI application source or actual hardware is loaded.
string suffix = Environment.GetEnvironmentVariable("AADC9_SHARED_MEMORY_NAME")
    ?? throw new Exception("An isolated test mapping name is required");
using var mutex = new Mutex(false, Environment.GetEnvironmentVariable("AADC9_SHARED_MEMORY_MUTEX_NAME"));
MemoryMappedFile? mapping = null;
var deadline = DateTime.UtcNow.AddSeconds(15);
while (mapping is null && DateTime.UtcNow < deadline) {
    try { mapping = MemoryMappedFile.OpenExisting(suffix, MemoryMappedFileRights.Read); }
    catch (FileNotFoundException) { await Task.Delay(50); }
}
using var mapped = mapping ?? throw new TimeoutException("Test shared memory missing");
using var view = mapped.CreateViewAccessor(0, 484, MemoryMappedFileAccess.Read);
using var pipe = new NamedPipeClientStream(".",
    Environment.GetEnvironmentVariable("AADC9_PIPE_NAME") ?? throw new Exception("Test pipe missing"),
    PipeDirection.InOut, PipeOptions.Asynchronous);
await pipe.ConnectAsync(5000);
uint sequence=0;
Console.WriteLine("{\"ready\":true}");
while (Console.ReadLine() is string line && line!="quit") {
    if (line=="frame") {
        if(!mutex.WaitOne(1000)) throw new TimeoutException("Test frame mutex");
        byte[] data=new byte[484];
        try { view.ReadArray(0,data,0,data.Length); } finally { mutex.ReleaseMutex(); }
        uint word(int at) => BinaryPrimitives.ReadUInt32LittleEndian(data.AsSpan(at,4));
        uint checksum=2166136261;
        foreach(byte b in data.AsSpan(32)) checksum=unchecked((checksum^b)*16777619);
        if(word(0)!=0x314d4354 || word(4)!=2 || word(8)!=484 || word(24)!=452 || word(28)!=checksum)
            throw new Exception("Invalid IPC frame");
        Console.WriteLine(JsonSerializer.Serialize(new { payload=new {
            targetCount=word(120), fcsAssets=new[] {new {
                isEnabled=word(72), isOperating=word(76), isConnected=word(80), laserOn=word(84), azimuthDeg=word(88)
            }}
        }}));
    } else {
        var q=JsonDocument.Parse(line).RootElement;
        uint kind=q.GetProperty("command").GetUInt32();
        uint[] fields={0x50495045,2,kind,++sequence,1,1,q.GetProperty("target").GetUInt32()};
        byte[] request=new byte[28];
        for(int i=0;i<fields.Length;i++) BinaryPrimitives.WriteUInt32LittleEndian(request.AsSpan(i*4,4),fields[i]);
        using var timeout=new CancellationTokenSource(TimeSpan.FromSeconds(4));
        await pipe.WriteAsync(request,timeout.Token);
        byte[] response=new byte[40]; await pipe.ReadExactlyAsync(response,timeout.Token);
        uint word(int at) => BinaryPrimitives.ReadUInt32LittleEndian(response.AsSpan(at,4));
        if(word(0)!=0x50495045 || word(4)!=2 || word(8)!=sequence || word(12)!=kind)
            throw new Exception("Mismatched IPC response");
        Console.WriteLine(JsonSerializer.Serialize(new {ResultCode=word(16),AssignedTargetId=word(24),
            AssignmentState=word(28),FireState=word(32),IsConnected=word(36)}));
    }
}
