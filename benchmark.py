import socket
import time
import threading

# --- Benchmarking Configuration ---
HOST = '127.0.0.1'       # Localhost
PORT = 6379              # Your C++ Server Port
REQUESTS_PER_WORKER = 1000
NUM_WORKERS = 5
# Total Requests = 5000

def worker(worker_id):
    try:
        # 1. Open a connection to your C++ server
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        
        # 2. Fire requests as fast as possible
        for i in range(REQUESTS_PER_WORKER):
            # Alternate between SET and GET commands
            if i % 2 == 0:
                command = f"SET key_{worker_id}_{i} benchmark_data 100\n"
            else:
                command = f"GET key_{worker_id}_{i-1}\n"
            
            # Send the command and wait for the C++ server to reply
            s.sendall(command.encode('utf-8'))
            response = s.recv(1024) 
            
        # 3. Close the door when done
        s.close()
    except Exception as e:
        print(f"Worker {worker_id} crashed: {e}")

if __name__ == "__main__":
    total_requests = NUM_WORKERS * REQUESTS_PER_WORKER
    print(f"Starting Stress Test: {total_requests} requests across {NUM_WORKERS} concurrent connections...")
    
    # Start the stopwatch
    start_time = time.time()

    # Unleash the workers
    threads = []
    for i in range(NUM_WORKERS):
        t = threading.Thread(target=worker, args=(i,))
        threads.append(t)
        t.start()

    # Wait for all workers to finish their jobs
    for t in threads:
        t.join()

    # Stop the stopwatch
    end_time = time.time()
    
    # Calculate the results
    duration = end_time - start_time
    requests_per_second = total_requests / duration

    print("-" * 30)
    print(f"Total Time: {duration:.4f} seconds")
    print(f"Throughput: {requests_per_second:.2f} Requests Per Second (RPS)")
    print("-" * 30)