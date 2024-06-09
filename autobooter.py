# original by plugnburn: https://gist.github.com/plugnburn/b3b0bcfd926c48ec5373bea84ce59337
# winreg edits lionscribe: https://forums.apps4flip.com/d/754/91
# 2024-06-08 neutronscott rewrote to allow argument to try other modes, and other small improvements

from winreg import ConnectRegistry, OpenKeyEx, QueryInfoKey, EnumValue, HKEY_LOCAL_MACHINE
from serial import Serial
import time
import sys

# Connect to the Windows Registry and search for MTK Preloader ports
r = ConnectRegistry(None, HKEY_LOCAL_MACHINE)
k = OpenKeyEx(r, r"SYSTEM\CurrentControlSet\Control\COM Name Arbiter\Devices")

# Find port
def get_ports():
    ports = []
    _,max,_ = QueryInfoKey(k)
    for i in range(max):
        port, loc, _ = EnumValue(k, i)
        if "vid_0e8d&pid_2000" in loc:
            print("#", end="", flush=True)
            ports.append(port)
        else:
            print(".", end="", flush=True)
    return ports

def try_port(port, bootseq):
    confirm = b"READY" + bytes(bootseq[:-4:-1], "ascii")

    try:
        s = Serial(port, 115200)
        print("\nConnected. Sending " + bootseq)
        s.write(bytes(bootseq, "ascii"))
        resp = s.read(8)
        if resp == confirm:
            print("Entered fastboot mode on port " + port)
            exitCode = 0
        else:
            print("Unknown response " + resp.decode("ascii") + " on port " + port)
            exitCode = 99
        s.close()
        sys.exit(exitCode)
    except OSError as e:
        print(".", end="", flush=True)

def main():
    print("Have USB attached and then remove/insert battery.")
    if len(sys.argv) > 1:
      bootseq = sys.argv[1]
    else:
      bootseq = "FASTBOOT"

    print("Searching registry for MTK Preloader ports", end="")
    ports = get_ports()
    if ports:
        print("\nFound MTK Preloader ports:")
        for port in ports:
            print("   " + port)
    else:
        print("\nNo MTK Preloader ports found!")
        sys.exit(100)

    print("Waiting for device", end="")
    while True:
        for port in ports:
            try_port(port, bootseq)
            time.sleep(0.1)

    print("\nFailed to enter fastboot mode!")
    sys.exit(99)
# main()

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\nUser Canceled!")
        sys.exit(101)
