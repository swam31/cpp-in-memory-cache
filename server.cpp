#include <iostream>
#include <string>
#include <unordered_map>
#include <list>
#include <queue>
#include <vector>
#include <chrono>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <shared_mutex>
#include <fstream>      // NEW: For reading and writing files to the hard drive

using namespace std;

struct CacheNode {
    string value;
    long long expiry_time;
    list<string>::iterator lru_ptr;
};

class InMemDatabase {
private:
    int max_capacity;
    unordered_map<string, CacheNode> store;
    list<string> lruList;
    priority_queue<
        pair<long long, string>, 
        vector<pair<long long, string>>, 
        greater<pair<long long, string>>
    > minHeap;
    
    shared_mutex db_mutex;

    long long getCurrentTime() {
        return chrono::duration_cast<chrono::seconds>(
            chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    void cleanExpiredKeys() {
        long long currentTime = getCurrentTime();
        while (!minHeap.empty() && minHeap.top().first <= currentTime) {
            string key = minHeap.top().second;
            long long heap_expiry = minHeap.top().first;
            minHeap.pop();

            if (store.find(key) != store.end() && store[key].expiry_time == heap_expiry) {
                lruList.erase(store[key].lru_ptr);
                store.erase(key);
                cout << "[System] GC removed expired key: " << key << endl;
            }
        }
    }

    // NEW: The Recovery Engine. Reads the file and rebuilds the database on startup.
    void restoreFromDisk() {
        ifstream file("database.aof");
        if (!file.is_open()) return; // If file doesn't exist yet, just start empty

        string key, value;
        int ttl;
        int count = 0;
        
        // Read the file line by line
        while (file >> key >> value >> ttl) {
            // We set 'is_recovery = true' so we don't accidentally write these 
            // back to the file while we are trying to read them!
            set(key, value, ttl, true); 
            count++;
        }
        cout << "[System] Successfully restored " << count << " keys from disk." << endl;
    }

public:
    InMemDatabase(int capacity) {
        max_capacity = capacity;
        restoreFromDisk(); // NEW: Boot up sequence triggers the recovery
    }

    // NEW: Added a hidden 'is_recovery' flag that defaults to false
    string set(string key, string value, int ttl_seconds, bool is_recovery = false) {
        unique_lock<shared_mutex> lock(db_mutex); 
        
        cleanExpiredKeys();
        long long expire_at = getCurrentTime() + ttl_seconds;

        if (store.find(key) != store.end()) {
            lruList.erase(store[key].lru_ptr);
        } else if (store.size() >= max_capacity) {
            string lru_key = lruList.back();
            lruList.pop_back();
            store.erase(lru_key);
        }

        lruList.push_front(key);
        store[key] = {value, expire_at, lruList.begin()};
        minHeap.push({expire_at, key});

        // NEW: If this is a normal user command, save it to the hard drive immediately!
        if (!is_recovery) {
            ofstream file("database.aof", ios::app); // ios::app means "Append to the end"
            file << key << " " << value << " " << ttl_seconds << "\n";
            file.flush(); // Force the OS to write to the physical disk right now
        }

        return "+OK\n";
    }

    string get(string key) {
        unique_lock<shared_mutex> lock(db_mutex);
        cleanExpiredKeys();

        if (store.find(key) != store.end()) {
            lruList.erase(store[key].lru_ptr);
            lruList.push_front(key);
            store[key].lru_ptr = lruList.begin();
            return "+" + store[key].value + "\n";
        }
        return "-ERR Key not found\n";
    }
};

void handle_client(int client_socket, InMemDatabase* db) {
    char buffer[1024];
    
    // NEW: Keep listening on this socket until the client hangs up
    while (true) {
        memset(buffer, 0, sizeof(buffer)); // Clear the buffer memory
        
        int bytes_read = read(client_socket, buffer, 1024);
        
        // If bytes_read is 0 or less, it means the Python client disconnected
        if (bytes_read <= 0) {
            break; 
        }
        
        string raw_command(buffer);
        istringstream iss(raw_command);
        vector<string> args;
        string word;
        while (iss >> word) { args.push_back(word); }

        string response = "";

        if (args.empty()) { response = "-ERR empty command\n"; } 
        else if (args[0] == "PING") { response = "+PONG\n"; }
        else if (args[0] == "SET" && args.size() >= 4) {
            int ttl = stoi(args[3]);
            response = db->set(args[1], args[2], ttl);
        }
        else if (args[0] == "GET" && args.size() >= 2) {
            response = db->get(args[1]);
        }
        else { response = "-ERR unknown command\n"; }

        send(client_socket, response.c_str(), response.length(), 0);
    }
    
    // Only close the door when the client breaks out of the while loop
    close(client_socket);
}

int main() {
    cout << "Starting Production Multi-Threaded Cache Server..." << endl;
    
    InMemDatabase db(100); 

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(6379);

    if (::bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        cout << "Error: Bind failed" << endl;
        return -1;
    }

    listen(server_fd, 10); 
    cout << "Server listening on port 6379..." << endl;

    while (true) {
        int addrlen = sizeof(address);
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        thread client_thread(handle_client, client_socket, &db);
        client_thread.detach(); 
    }

    return 0;
}
