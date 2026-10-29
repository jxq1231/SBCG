"""Bounded SBCG request/reply test. RTT is host roundtrip, not clock accuracy."""
import argparse
import json
from pathlib import Path
import socket
import sys
import time

from udp_monitor import WIRE, ipv4, uid_words


def decode_ack(data):
    if len(data) != WIRE.size:
        raise ValueError("ACK payload must be 28 bytes")
    magic,version,kind,length,sequence,tick_ms,*uid = WIRE.unpack(data)
    if (magic,version,kind,length) != (b"SBCG",1,3,28):
        raise ValueError("unsupported ACK magic/version/type/length")
    return sequence,tick_ms,tuple(uid)


def run_probe(sock, target, uid, count, interval_s, timeout_s, emit):
    sent = received = timeouts = send_errors = invalid = foreign = unexpected = 0
    rtt_ms = []
    start = next_send = time.monotonic()
    stop_reason = "count"
    emit({"event":"ready","bind":sock.getsockname(),"target":target,"count":count})
    try:
        for sequence in range(1,count+1):
            while time.monotonic() < next_send:
                time.sleep(min(0.05,max(0,next_send-time.monotonic())))
            request_at = time.monotonic()
            client_tick_ms = int(request_at*1000) & 0xFFFFFFFF
            next_send = request_at + interval_s
            data = WIRE.pack(b"SBCG",1,2,28,sequence,client_tick_ms,*uid)
            try:
                sock.sendto(data,target)
            except OSError as exc:
                send_errors += 1
                emit({"event":"send_error","sequence":sequence,"reason":str(exc)})
                continue
            sent += 1
            emit({"event":"request","sequence":sequence,"client_tick_ms":client_tick_ms})
            deadline = request_at + timeout_s
            matched = False
            while time.monotonic() < deadline:
                sock.settimeout(max(0.001,min(0.1,deadline-time.monotonic())))
                try:
                    reply,address = sock.recvfrom(65535)
                    reply_at = time.monotonic()
                except socket.timeout:
                    continue
                except ConnectionResetError as exc:
                    emit({"event":"socket_error","sequence":sequence,"reason":str(exc)})
                    break
                if address != target:
                    foreign += 1
                    continue
                try:
                    ack_seq,echo_tick_ms,board_uid = decode_ack(reply)
                    if board_uid != uid:
                        raise ValueError("ACK UID differs from target")
                except ValueError as exc:
                    invalid += 1
                    emit({"event":"invalid","reason":str(exc)})
                    continue
                if ack_seq != sequence or echo_tick_ms != client_tick_ms:
                    unexpected += 1
                    emit({"event":"unexpected","expected":sequence,"received":ack_seq,
                          "expected_tick_ms":client_tick_ms,"echo_tick_ms":echo_tick_ms})
                    continue
                elapsed_ms = (reply_at-request_at)*1000
                rtt_ms.append(elapsed_ms)
                received += 1
                matched = True
                emit({"event":"ack","sequence":sequence,"echo_tick_ms":echo_tick_ms,
                      "uid":[f"{word:08X}" for word in uid],"source":address,
                      "rtt_ms":round(elapsed_ms,3)})
                break
            if not matched:
                timeouts += 1
                emit({"event":"timeout","sequence":sequence})
    except KeyboardInterrupt:
        stop_reason = "interrupted"
    result = {"event":"summary","requested":count,"sent":sent,"received":received,
              "timeouts":timeouts,"send_errors":send_errors,"invalid":invalid,
              "foreign":foreign,"unexpected":unexpected,"unanswered":sent-received,
              "duration_s":round(time.monotonic()-start,3),"stop_reason":stop_reason,
              "rtt_ms_min":round(min(rtt_ms),3) if rtt_ms else None,
              "rtt_ms_max":round(max(rtt_ms),3) if rtt_ms else None,
              "rtt_ms_mean":round(sum(rtt_ms)/len(rtt_ms),3) if rtt_ms else None,
              "status":"PASS" if received==count and send_errors==0 and invalid==0
              and stop_reason=="count" else "REVIEW","hardware_verified":False}
    emit(result)
    return result


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind",type=ipv4,default="192.168.10.100")
    parser.add_argument("--port",type=int,default=6006)
    parser.add_argument("--target",type=ipv4,default="192.168.10.1")
    parser.add_argument("--target-port",type=int,default=5006)
    parser.add_argument("--uid",type=uid_words,required=True,help="Target W0:W1:W2 in hex")
    parser.add_argument("--count",type=int,default=30)
    parser.add_argument("--interval",type=float,default=1)
    parser.add_argument("--timeout",type=float,default=0.8)
    parser.add_argument("--log",type=Path)
    args=parser.parse_args(argv)
    if not (1<=args.port<=65535 and 1<=args.target_port<=65535 and 1<=args.count<=3600):
        parser.error("ports must be 1..65535 and count 1..3600")
    if not (0.1<=args.interval<=60 and 0<args.timeout<=60):
        parser.error("interval must be 0.1..60 seconds and timeout 0..60 seconds")
    output=None
    try:
        if args.log:
            args.log.parent.mkdir(parents=True,exist_ok=True)
            output=args.log.open("x",encoding="utf-8")

        def emit(record):
            line=json.dumps(record,ensure_ascii=False)
            print(line,flush=True)
            if output:
                output.write(line+"\n");output.flush()

        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
            sock.bind((args.bind,args.port))
            result=run_probe(sock,(args.target,args.target_port),args.uid,args.count,
                             args.interval,args.timeout,emit)
        return 0 if result["status"]=="PASS" else 1
    except OSError as exc:
        print(f"UDP probe error: {exc}",file=sys.stderr)
        return 2
    finally:
        if output:output.close()


if __name__=="__main__":
    sys.exit(main())
