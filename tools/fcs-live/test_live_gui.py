"""Real C++ live station + fake UDP FCS/RDR + original C# GUI IPC reader."""
from pathlib import Path
import json
import os
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools' / 'fcs-live'))
from rdr_sim.server import RdrServer


class FakeFcs:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.settimeout(.05)
        self.port = self.socket.getsockname()[1]
        self.running = True
        self.requests = []
        self.pan, self.tilt, self.demo_until = 180, 0, 0
        self.drop_status = self.fault = self.drop_demo = False
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def run(self):
        seq = 0
        while self.running:
            try:
                data, source = self.socket.recvfrom(256)
            except socket.timeout:
                continue
            kind, request, rx = struct.unpack_from('!HHH', data)
            self.requests.append(kind)
            if kind == 0xEF02 and self.drop_status:
                continue
            assert rx == 0 and kind in range(0xEF01, 0xEF06)
            if kind == 0xEF03:
                self.tilt, self.pan = struct.unpack_from('!hh', data, 6)
            elif kind == 0xEF04:
                assert struct.unpack_from('!H', data, 6)[0] == 0
                self.demo_until = 0
            elif kind == 0xEF05:
                self.demo_until = time.monotonic() + 1.5
                if self.drop_demo:
                    continue
            reply = struct.pack('!HHH', kind - 0xEF00 + 0xFE00, seq, request)
            seq = (seq + 1) & 65535
            if kind == 0xEF02:
                reply += struct.pack('!Hhh', 17 if self.fault else 13 if self.demo_until > time.monotonic() else 1,
                                     self.tilt, self.pan)
            elif kind == 0xEF04:
                reply += struct.pack('!HHH', 0, 0, 0)
            else:
                reply += struct.pack('!H', 0)
            self.socket.sendto(reply, source)

    def close(self):
        self.running = False
        self.thread.join(2)
        self.socket.close()


def main(original=False):
    folder = ROOT / 'build/fcs-live-original-gui' / ('test-' + uuid.uuid4().hex)
    folder.mkdir(parents=True)
    stop = folder / 'stop'
    stop.unlink(missing_ok=True)
    fcs = FakeFcs()
    # Independent ports and IPC names: running real hardware sessions are untouched.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(('127.0.0.1', 0))
        rdr_local_port = reservation.getsockname()[1]
    rdr = RdrServer(('127.0.0.1', 0), ('127.0.0.1', rdr_local_port)).start()
    suffix = '.test-' + uuid.uuid4().hex
    env = dict(os.environ, AADC9_SHARED_MEMORY_NAME='Local\\ControlStationMock.SharedMemory.v2'+suffix,
               AADC9_SHARED_MEMORY_MUTEX_NAME='Local\\ControlStationMock.SharedMemory.Mutex.v2'+suffix,
               AADC9_PIPE_NAME='ECS_NamedPipe'+suffix)
    backend_output=(folder/'stdout.log').open('w')
    backend_errors=(folder/'stderr.log').open('w')
    backend = subprocess.Popen([str(ROOT / 'build/fcs-live-original-gui/Release/c2_fcs_gui.exe'),
        '--bind', '127.0.0.1', '--fcs-ip', '127.0.0.1', '--fcs-port', str(fcs.port),
        *([] if original else ['--gui-extended']), '--rdr-port',str(rdr.address[1]),'--rdr-local-port',str(rdr_local_port),'--ipc-suffix',suffix,
        '--status-file',str(folder/'status.json'),'--stop-file', str(stop)], stdout=backend_output, stderr=backend_errors, text=True)
    probe = None
    try:
        probe = subprocess.Popen(['dotnet', str(ROOT / 'build/fcs-live-original-gui/probe/GuiProbe.dll')],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,env=env)
        messages = queue.Queue()
        def pump():
            for line in probe.stdout:
                messages.put(json.loads(line))
        threading.Thread(target=pump, daemon=True).start()
        assert messages.get(timeout=15)['ready']

        def query(value):
            probe.stdin.write((value if isinstance(value, str) else json.dumps(value)) + '\n')
            probe.stdin.flush()
            return messages.get(timeout=4)

        def command(kind, target=0):
            return query({'command': kind, 'target': target})

        def wait(predicate, timeout=8):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                result = predicate()
                if result:
                    return result
                time.sleep(.05)
            raise AssertionError('live GUI condition timed out')

        def ready_frame():
            p = query('frame')['payload']
            return p if p['targetCount'] == 1 and p['fcsAssets'][0]['isEnabled'] else None

        frame = wait(ready_frame)
        assert frame['fcsAssets'][0]['isOperating'] == 1
        # Telemetry conversion must never issue motor/output commands.
        assert frame['fcsAssets'][0]['azimuthDeg'] == 0  # motor north = 180
        for motor, displayed in ((120, 300), (179, 359), (181, 1), (240, 60),
                                 (0, 180), (360, 180), (-32768, 0), (180, 0)):
            fcs.pan = motor
            wait(lambda: query('frame')['payload']['fcsAssets'][0]['azimuthDeg'] == displayed)
        wait(ready_frame)  # wait for valid telemetry after the sentinel case
        assert all(k in (0xEF01, 0xEF02) for k in fcs.requests), fcs.requests
        assert rdr.snapshot()['acked'] > 0
        assert command(1, 1)['ResultCode'] == 0
        wait(lambda: command(0)['AssignmentState'] == 2)
        assert (fcs.pan, fcs.tilt) == (184, 2)
        wait(lambda: query('frame')['payload']['fcsAssets'][0]['azimuthDeg']==4)
        if original:
            fcs.drop_status=True
            wait(lambda: not query('frame')['payload']['fcsAssets'][0]['isEnabled'])
            status=command(0)
            assert status['AssignmentState']==1 and status['ResultCode']==0
            assert command(3,1)['ResultCode']==2
            assert 0xEF05 not in fcs.requests
            fcs.drop_status=False
            wait(lambda: command(0)['AssignmentState']==2,timeout=12)
            fcs.fault=True
            wait(lambda: command(0)['AssignmentState']==3)
            assert command(3,1)['ResultCode']==6
            assert command(2,1)['ResultCode']==0
            print('PASS: upstream GUI codes only; waiting blocks commands; fault handling; north-zero azimuth')
            return
        points=fcs.requests.count(0xEF03)
        fcs.drop_status=True
        wait(lambda: command(0)['AssignmentState']==4)
        assert command(3,1)['ResultCode']==7
        assert fcs.requests.count(0xEF05)==0
        fcs.drop_status=False
        wait(lambda: command(0)['AssignmentState']==2,timeout=12)
        assert fcs.requests.count(0xEF03)==points
        rdr.remove_target(1)
        wait(lambda: command(0)['ResultCode']==8)
        assert command(3,1)['ResultCode']==8
        from rdr_sim import wire
        rdr.set_target(wire.Target(1,3,.2,.1))
        wait(lambda: command(0)['AssignmentState']==2)
        assert fcs.requests.count(0xEF03)==points
        rdr.set_target(wire.Target(1,3,.4,.1))
        wait(lambda: command(0)['ResultCode']==9)
        assert command(3,1)['ResultCode']==9
        assert command(1,1)['ResultCode']==0
        wait(lambda: command(0)['AssignmentState']==2)
        fcs.fault=True
        wait(lambda: command(0)['ResultCode']==10)
        wait(lambda: not query('frame')['payload']['fcsAssets'][0]['isEnabled'])
        fcs.fault=False
        wait(ready_frame)
        assert command(0)['AssignmentState']==3
        assert command(3,1)['ResultCode']==10
        assert command(1,1)['ResultCode']==0
        wait(lambda: command(0)['AssignmentState']==2)
        assert command(3, 1)['ResultCode'] == 0
        wait(lambda: command(0)['FireState'] == 1)
        assert fcs.requests.count(0xEF05) == 1
        assert command(2, 1)['ResultCode'] == 0
        assert command(0)['AssignmentState'] == 0
        assert command(1,1)['ResultCode']==0
        wait(lambda: command(0)['AssignmentState']==2)
        fcs.drop_demo=True
        assert command(3,1)['ResultCode']==13
        assert command(3,1)['ResultCode']==13
        assert fcs.requests.count(0xEF05)==2
        assert command(2,1)['ResultCode']==0
        assert 0xEF04 in fcs.requests
        assert command(4, 15)['ResultCode'] == 5
        assert command(99)['ResultCode'] == 5
        assert 'response_timeout outcome_unknown' in (folder/'events.log').read_text()
        print('PASS: isolated live IPC; temporary FCS/RDR loss recovers without commands; '
              'changed targets/faults remain blocked; explicit reassignment; demo completion; '
              'unknown outcome blocks retry; cancellation; persistent diagnostic reasons')
    finally:
        if probe:
            try:
                probe.communicate('quit\n', timeout=3)
            except subprocess.TimeoutExpired:
                probe.kill(); probe.communicate(timeout=3)
        stop.write_text('stop', encoding='utf-8')
        try:
            backend.wait(timeout=4)
        except subprocess.TimeoutExpired:
            backend.kill(); backend.wait(timeout=3)
        backend_output.close(); backend_errors.close()
        rdr.close(); fcs.close()
        stop.unlink(missing_ok=True)
        if backend.returncode:
            raise AssertionError(((folder/'stdout.log').read_text(), (folder/'stderr.log').read_text()))


if __name__ == '__main__':
    main(original='--original' in sys.argv)
