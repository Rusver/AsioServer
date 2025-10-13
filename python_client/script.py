# This is a sample Python script.

# Press Shift+F10 to execute it or replace it with your code.
# Press Double Shift to search everywhere for classes, files, tool windows, actions, and settings.

import socket
import struct
import random
import os

# === OP CODES ===
OP_SAVE = 100
OP_GET = 200
OP_DELETE = 201
OP_LIST = 202

# === helper functions ===
def read_server_info():
    with open("server.info", "r") as f:
        ip, port = f.read().strip().split(":")
        return ip, int(port)

def read_backup_info():
    with open("backup.info", "r") as f:
        return [line.strip() for line in f.readlines() if line.strip()]

def send_all(sock, data):
    total_sent = 0
    while total_sent < len(data):
        sent = sock.send(data[total_sent:])
        if sent == 0:
            raise RuntimeError("Socket connection broken")
        total_sent += sent

def recv_all(sock, size):
    buf = b''
    while len(buf) < size:
        chunk = sock.recv(size - len(buf))
        if not chunk:
            raise RuntimeError("Connection closed")
        buf += chunk
    return buf

# === protocol functions ===
def send_request(sock, user_id, version, op, filename, payload=b''):
    name_bytes = filename.encode('ascii')
    hdr = struct.pack('<IBBH', user_id, version, op, len(name_bytes))
    send_all(sock, hdr + name_bytes)

    if payload:
        payload_hdr = struct.pack('<I', len(payload))
        send_all(sock, payload_hdr + payload)

def receive_response(sock, save_as=None):
    """Receive structured server response and handle known response codes."""
    try:
        header = recv_all(sock, 3)
        if len(header) < 3:
            print("Error: Invalid or incomplete response header.")
            return None

        version, code = struct.unpack("<BH", header)  # B = uint8, H = uint16 little-endian

        # === Handle known response codes ===
        if code == 210:
            print("210 OK — Operation successful.")
        elif code == 211:
            print("211 The file list is OK")
        elif code == 212:
            print("212 the backup / file deletion succeeded")
        elif code == 1001:
            print("1001 Error — Invalid request.")
            return None
        elif code == 1002:
            print("1002 Error — File not found.")
            return None
        elif code == 1003:
            print("1003 Error — Server-side failure.")
            return None
        else:
            print(f"Unknown server code: {code}")

        if code in (210, 211, 212):  # success codes with optional payload
            # Try to read payload header if message exists
            payload_header = recv_all(sock, 4)
            (size,) = struct.unpack("<I", payload_header)
            if size > 0:
                data = recv_all(sock, size)
                if save_as:
                    with open(save_as, "wb") as f:
                        f.write(data)
                        print(f"💾 Saved payload as {save_as}")
                return data

    except Exception as e:
        print(f"Error receiving response: {e}")
        return None


def parse_file_list(file_bytes):
    """Convert bytes of list file into Python list of filenames."""
    return file_bytes.decode('ascii').splitlines()


# === main flow ===
def main():
    user_id = random.randint(1, 0xFFFFFFFF)
    version = 1

    server_ip, server_port = read_server_info()
    filenames = read_backup_info()

    print(f"Client ID: {user_id}")

    # Reconnect per command (simple approach)
    def reconnect():
        new_s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        new_s.connect((server_ip, server_port))
        return new_s

    with reconnect() as s:

        # 1️ request list
        print("Requesting file list...")
        send_request(s, user_id, version, OP_LIST, "")
        list_bytes = receive_response(s, save_as="file_list.txt")  # save to file
        if list_bytes:
            list_filename = list_bytes.decode("ascii").strip()
            print(f"Server created list file: {list_filename}")

            # Now request the actual list file
            with reconnect() as s2:
                send_request(s2, user_id, version, OP_GET, list_filename)
                list_data = receive_response(s2)
                if list_data:
                    with open("file_list.txt", "wb") as f:
                        f.write(list_data)
                    print("V Saved server file list as file_list.txt")

                    # Print actual file list
                    print("Files on server:")
                    print(list_data.decode(errors="ignore"))

        else:
            print("Error Server Failed to get list filename or the file is empty.")



    # 2-3 Upload all files
    if not filenames:
        print("No files found in backup.info — nothing to upload.")
        return
    else:
        for filename in filenames:
            with reconnect() as s:
                with open(filename, "rb") as f:
                    data = f.read()
                print(f"Uploading {filename} ({len(data)} bytes)...")
                send_request(s, user_id, version, OP_SAVE, filename, data)
                print(receive_response(s))


    # 4️ list again
    with reconnect() as s:
        print("Listing files again...")
        send_request(s, user_id, version, OP_LIST, "")
        list_bytes = receive_response(s, save_as="file_list.txt")  # save to file
        if list_bytes:
            list_filename = list_bytes.decode("ascii").strip()
            print(f"Server created list file: {list_filename}")

            # Now request the actual list file
            with reconnect() as s2:
                send_request(s2, user_id, version, OP_GET, list_filename)
                list_data = receive_response(s2)
                if list_data:
                    with open("file_list.txt", "wb") as f:
                        f.write(list_data)
                    print("V Saved server file list as file_list.txt")

                    # Print actual file list
                    print("Files on server:")
                    print(list_data.decode(errors="ignore"))

        else:
            print("2 Error Server Failed to get list filename.")

    # 5 get first file
    with reconnect() as s:
        filename = filenames[0]
        print(f"Downloading {filename}...")
        send_request(s, user_id, version, OP_GET, filename)
        data = receive_response(s)
        with open("tmp_" + filename, "wb") as out:
            out.write(data)
        print("File saved as tmp_" + filename)

    # 6 delete first file
    with reconnect() as s:
        filename = filenames[0]
        print(f"Deleting {filename}...")
        send_request(s, user_id, version, OP_DELETE, filename)
        print(receive_response(s))

    # 7 try to get deleted file
    with reconnect() as s:
        filename = filenames[0]
        print(f"Trying to download deleted file {filename}...")
        send_request(s, user_id, version, OP_GET, filename)
        print(receive_response(s))

    print("Done V")

if __name__ == "__main__":
    main()

# See PyCharm help at https://www.jetbrains.com/help/pycharm/
