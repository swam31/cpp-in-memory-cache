#include <iostream>
#include <unordered_map>
#include <string>
#include <list>
#include <queue>
#include <vector>
#include <chrono>
#include <thread>

using namespace std;

// The complete data package stored in our Hash Map
struct CacheNode {
    string value;
    long long expiry_time;
    list<string>::iterator lru_ptr; // Pointer to its location in the LRU list
};

class InMemDatabase {
private:
    int max_capacity;
    
    // 1. Hash Map: O(1) lookups
    unordered_map<string, CacheNode> store;
    
    // 2. Doubly Linked List: Tracks LRU (Front = Newest, Back = Oldest)
    list<string> lruList;
    
    // 3. Min-Heap: Tracks Time-To-Live (Top = Closest to expiring)
    priority_queue<
        pair<long long, string>, 
        vector<pair<long long, string>>, 
        greater<pair<long long, string>>
    > minHeap;

    long long getCurrentTime() {
        return chrono::duration_cast<chrono::seconds>(
            chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // Active Garbage Collector
    void cleanExpiredKeys() {
        long long currentTime = getCurrentTime();

        while (!minHeap.empty() && minHeap.top().first <= currentTime) {
            string key = minHeap.top().second;
            long long heap_expiry = minHeap.top().first;
            minHeap.pop();

            // GHOST CHECK: Does the key still exist, and does the time match?
            if (store.find(key) != store.end() && store[key].expiry_time == heap_expiry) {
                // It's a valid expiration! Remove from list and map.
                lruList.erase(store[key].lru_ptr);
                store.erase(key);
                cout << "[Time Expiration] Garbage collected: " << key << endl;
            }
        }
    }

public:
    InMemDatabase(int capacity) {
        max_capacity = capacity;
    }

    void set(string key, string value, int ttl_seconds) {
        cleanExpiredKeys(); // Always clean up before adding new data
        
        long long expire_at = getCurrentTime() + ttl_seconds;

        // If key already exists, remove its old position in the LRU list
        if (store.find(key) != store.end()) {
            lruList.erase(store[key].lru_ptr);
        } 
        // If memory is full, execute Space-Based Eviction (LRU)
        else if (store.size() >= max_capacity) {
            string lru_key = lruList.back();
            lruList.pop_back();
            store.erase(lru_key);
            cout << "[Space Eviction] Memory Full! LRU removed: " << lru_key << endl;
        }

        // Insert at the front of the list (Most Recently Used)
        lruList.push_front(key);
        
        // Save everything to the Hash Map
        store[key] = {value, expire_at, lruList.begin()};
        
        // Push to the Min-Heap for TTL tracking
        minHeap.push({expire_at, key});

        cout << "Saved: [" << key << " -> " << value << "] (TTL: " << ttl_seconds << "s)" << endl;
    }

    string get(string key) {
        cleanExpiredKeys(); // Ensure we don't fetch expired data

        if (store.find(key) != store.end()) {
            // Move to the front of the LRU list because it was just accessed
            lruList.erase(store[key].lru_ptr);
            lruList.push_front(key);
            store[key].lru_ptr = lruList.begin(); // Update pointer
            
            return store[key].value;
        }
        return "Error: Key not found!";
    }
};

int main() {
    cout << "Starting Production-Grade In-Memory Cache..." << endl;
    cout << "--------------------------------------------" << endl;

    InMemDatabase db(3); // Strict limit of 3 keys

    // 1. Test Time-Based Eviction (TTL)
    db.set("Session", "User123", 2); 
    
    cout << "\nWaiting 3 seconds..." << endl;
    this_thread::sleep_for(chrono::seconds(3));
    
    cout << "Fetching Session: " << db.get("Session") << " (Should trigger Time Expiration)\n" << endl;

    // 2. Test Space-Based Eviction (LRU)
    db.set("A", "Apple", 100);
    db.set("B", "Banana", 100);
    db.set("C", "Cherry", 100);
    
    // Fetch A to make it recently used
    cout << "Fetching A: " << db.get("A") << endl; 
    
    // Add D to exceed capacity (Should trigger Space Eviction of 'B')
    cout << "\nAdding 'D' (Memory Limit Reached)..." << endl;
    db.set("D", "Date", 100);

    return 0;
}