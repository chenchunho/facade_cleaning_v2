#!/usr/bin/env python3
"""
depth_cam_test_client.py — tiny interactive TCP client for manually testing
depth_cam_service.py (which has no keyboard UI of its own — see that file's
module docstring). Run this ON THE PI, in an SSH session, while
depth_cam_service.py is already running in the background.

Usage:
    python3 depth_cam_test_client.py

Prompts:
    ENTER  -> sends BEFORE
    ENTER  -> (after you've physically shifted the camera/target ~1-2cm,
               pure lateral shift, no rotation, keep the background/reflection
               static — see project notes on why a bad shift over-masks)
               sends AFTER, prints the result
    q ENTER -> quit

View the result photo from any browser on the same network at:
    http://192.168.5.25:5008/snap/depth
(only updates after an AFTER call that actually found + drew candidates —
see depth_cam_service.py's handle_after: a capture with 0 candidates or a
failed plane fit does not publish a new photo, so the endpoint may still show
an OLDER photo, or 503 if none has ever been published this run).
"""

import socket
import sys

HOST = "127.0.0.1"
PORT = 9530


def send(sock, line):
    sock.sendall((line + "\n").encode("utf-8"))
    return sock.recv(1024).decode("utf-8", errors="replace").strip()


def main():
    sock = socket.create_connection((HOST, PORT), timeout=10)
    print(f"[test_client] connected to {HOST}:{PORT}")
    print(f"[test_client] {send(sock, 'PING')}")

    try:
        while True:
            input("\nPress ENTER to capture BEFORE (or 'q' to quit)... ")
            reply = send(sock, "BEFORE")
            print(f"[test_client] BEFORE -> {reply}")
            if reply.rfind("OK", 0) != 0:
                print("[test_client] BEFORE failed, try again")
                continue

            input("Now shift the camera/target ~1-2cm (pure lateral, keep "
                  "background static), then press ENTER to capture AFTER... ")
            reply = send(sock, "AFTER")
            print(f"[test_client] AFTER  -> {reply}")
            print("[test_client] view result photo: http://192.168.5.25:5008/snap/depth")

            again = input("\nAnother round? (ENTER=yes, q=quit) ")
            if again.strip().lower() == "q":
                break
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        sock.close()
        print("[test_client] done")


if __name__ == "__main__":
    main()
