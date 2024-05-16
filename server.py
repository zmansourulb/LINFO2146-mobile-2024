import socket
import argparse
import json
import time

X = 2
Y = 10

GATEWAY = 0
SUBGATEWAY = 1
SENSOR = 2

NULL_MSG = 0
HELLO = 1
HELLO_ACK = 2
CHILD_DISCONNECT = 3
APPLICATION = 4

NULL_APP = 0
APP_LGT_LVL = 1
APP_LGT_ON = 2
APP_IRG_ON = 3
APP_IRG_ACK = 4

NO_CAT = 0
IRG_SYS = 1
MOB_TER = 2
LGT_SEN = 3
LGT_BLB = 4

def recv(sock):
    data = sock.recv(1)
    buf = b""
    while data.decode("utf-8") != "\n":
        buf += data
        data = sock.recv(1)
    return buf

def main(ip, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ip, port))

    tick = 0

    while True:
        data = recv(sock)
        data = data.decode("utf-8")
        serv_token = "[2serv]"
        clie_token = "[2clie]"
        if data.startswith(serv_token):
            try:
                data = data[len(serv_token):]
                rpacket = json.loads(data)
                if rpacket["rank"] == SENSOR and rpacket["msgcat"] == APPLICATION:
                    print(f"[ADDR {rpacket['src']}]", end="")
                    if rpacket["appcat"] == APP_LGT_LVL:
                        print(f" light value: {rpacket['value']:02d}", end="")
                        if rpacket["value"] < 20:
                            spacket = f"{clie_token}{GATEWAY}|{APPLICATION}|{APP_LGT_ON}|{X}|{rpacket['src']}\n".encode("utf-8")
                            sock.send(spacket)
                            print(f" -> set lights on for {X:02d} sec...", end="")
                    elif rpacket["appcat"] == APP_IRG_ACK:
                        if rpacket["value"] == 1:
                            print(f" irrigation is on...", end="")
                        else:
                            print(f" irrigation is off.", end="")
                print()
            except:
                print("Error when decoding JSON:", data)
        
        if tick % 20 == 0:
            spacket = f"{clie_token}{GATEWAY}|{APPLICATION}|{APP_IRG_ON}|{Y}|{rpacket['src']}\n".encode("utf-8")
            sock.send(spacket)
            print(f"[ADDR xxxx.xxxx.xxxx.xxxx] -> set irrigation on for {Y:02d} sec...")
        
        tick += 1

if __name__ == "__main__":

    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", dest="ip", type=str)
    parser.add_argument("--port", dest="port", type=int)
    args = parser.parse_args()

    main(args.ip, args.port)
