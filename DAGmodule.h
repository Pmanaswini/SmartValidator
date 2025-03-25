#include <pthread.h>

#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "block.pb.h"
#include "components.pb.h"
#include "matrix.pb.h"
#include "transaction.pb.h"

using namespace std;

struct TransactionStruct {
  int txn_no;
  int inputscount;         // Number of input addresses
  vector<string> inputs;   // Input addresses
  int outputcount;         // Number of output addresses
  vector<string> outputs;  // Output addresses
};

struct Node {
  int value; 
  Node* next;
  Node(int val) : value(val), next(nullptr) {}
};


struct AddressData {
  atomic<Node*> head{nullptr};
  atomic<int> writeID{-1}; // Initialize writeID to -1
  
  // Delete copy constructor and assignment operator
  AddressData(const AddressData&) = delete;
  AddressData& operator=(const AddressData&) = delete;
  
  // Allow move operations
  AddressData(AddressData&&) noexcept = default;
  AddressData& operator=(AddressData&&) noexcept = default;
  
  // Default constructor
  AddressData() = default;
  
  ~AddressData() {
    // Delete all nodes in the linked list
    Node* current = head.load();
    while (current != nullptr) {
      Node* next = current->next;
      delete current;
      current = next;
    }
  }
};

class DAGmodule {
 public:
  vector<TransactionStruct> CurrentTransactions;
  vector<vector<int>> adjacencyMatrix;
  unique_ptr<std::atomic<int>[]> inDegree;
  atomic<int> completedTxns{0}, lastTxn{0};  // Global atomic counter
  int totalTxns,
      threadCount = 1;  // threadcount can be input or set based on the cores
  components::componentsTable cTable;
  vector<unique_ptr<AddressData>> addressArray;  // Changed to vector of unique_ptr
  static const int ADDRESS_DATA_SIZE = 2400;

  // Constructor
  DAGmodule() {}

  TransactionStruct extractTransaction(const transaction::Transaction& tx,
                                       int position) {
    // Initialize TransactionInfo struct
    TransactionStruct txn;
    txn.txn_no = position;

    // Deserialize the transaction header
    transaction::TransactionHeader txHeader;
    if (!txHeader.ParseFromString(tx.header())) {
      std::cerr << "Failed to parse TransactionHeader." << std::endl;
      return txn;
    }

    // Fill input addresses
    txn.inputscount = txHeader.inputs_size();
    for (const auto& input : txHeader.inputs()) {
      txn.inputs.push_back(input);
    }

    // Fill output addresses
    txn.outputcount = txHeader.outputs_size();
    for (const auto& output : txHeader.outputs()) {
      txn.outputs.push_back(output);
    }

    return txn;
  }

  void dependencyMatrix(int PID) {
    int txn, inp_lo, out_lo;
    while (true) {
      txn = lastTxn.fetch_add(1);
      if (txn >= totalTxns) {
        lastTxn.fetch_sub(1);
        return;
      }

      inp_lo = CurrentTransactions[txn].inputscount;
      out_lo = CurrentTransactions[txn].outputcount;
      
      // Check input dependencies against previous transactions' outputs
      for (int j = 0; j < inp_lo; j++) {
        bool flag = false;
        int txn2;
        
        for (int i = txn - 1; i >= 0 && !flag; i--) {
          if (inDegree[i] > -1) {
            int out_ex = CurrentTransactions[i].outputcount;

            for (int k = 0; k < out_ex; k++) {
              if (CurrentTransactions[txn].inputs[j] == CurrentTransactions[i].outputs[k]) {
                flag = true;
                txn2 = i;
                break;
              }
            }
          }
        }

        if (flag) {
          if (adjacencyMatrix[CurrentTransactions[txn2].txn_no][txn] == 0) {
            adjacencyMatrix[CurrentTransactions[txn2].txn_no][txn] = 1;
            inDegree[txn].fetch_add(1, std::memory_order_relaxed);
          }
        }
      }

      // Check output dependencies against previous transactions
      for (int j = 0; j < out_lo; j++) {
        bool flag = false;
        int txn2;
        
        for (int i = txn - 1; i >= 0 && !flag; i--) {
          if (inDegree[i] > -1) {
            int inp_ex = CurrentTransactions[i].inputscount;
            int out_ex = CurrentTransactions[i].outputcount;

            // Check against outputs
            for (int k = 0; k < out_ex; k++) {
              if (CurrentTransactions[txn].outputs[j] == CurrentTransactions[i].outputs[k]) {
                flag = true;
                txn2 = i;
                break;
              }
            }

            // Check against inputs
            for (int k = 0; k < inp_ex && !flag; k++) {
              if (CurrentTransactions[txn].outputs[j] == CurrentTransactions[i].inputs[k]) {
                flag = true;
                txn2 = i;
                break;
              }
            }
          }
        }

        if (flag) {
          if (adjacencyMatrix[CurrentTransactions[txn2].txn_no][txn] == 0) {
            adjacencyMatrix[CurrentTransactions[txn2].txn_no][txn] = 1; 
            inDegree[txn].fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    }
  }

  void DFSUtil(int v, vector<bool>& visited,
               components::componentsTable::component* component) {
    // Mark the current node as visited and print it
    auto* txn = component->add_transactionlist();
    txn->set_id(v);
    visited[v] = true;
    int n = adjacencyMatrix.size();
    // Recur for all vertices adjacent to this vertex
    for (int i = 0; i < n; i++) {
      if (adjacencyMatrix[v][i] && !visited[i]) {
        DFSUtil(i, visited, component);
      }
    }
  }

  string connectedComponents() {
    int n = adjacencyMatrix.size();
    int totalComponents = 0;
    vector<bool> visited(n, false);
    string output;

    for (int i = 0; i < n; i++) {
      if (!visited[i]) {
        auto* component = cTable.add_componentslist();
        // Print all reachable vertices from v
        DFSUtil(i, visited, component);
        totalComponents++;
      }
    }
    cTable.set_totalcomponents(totalComponents);

    if (cTable.SerializeToString(&output)) {
      return output;
    } else {
      std::cerr << "Failed to serialize the message." << std::endl;
      return "";
    }
  }

  // Function to serialize adjacency matrix to DirectedGraph proto
  std::string serializeDAG() {
    matrix::DirectedGraph graphProto;
    graphProto.set_num_nodes(adjacencyMatrix.size());

    for (const auto& row : adjacencyMatrix) {
      matrix::DirectedGraph::MatrixRow* matrixRow =
          graphProto.add_adjacencymatrix();
      for (int edge : row) {
        matrixRow->add_edges(edge);
      }
    }

    // Serialize the protobuf message to a string
    std::string serializedData;
    if (!graphProto.SerializeToString(&serializedData)) {
      cerr << "Failed to serialize the adjacency matrix." << endl;
      return "";
    }
    return serializedData;
  }

  // Function to create DAG from block.proto
  bool create(const string& blockData) {
    Block block;
    int i;
    thread threads[threadCount];

    // Deserialize blockProtoData into block object
    if (block.ParseFromString(blockData)) {
      int position = 0;
      for (const auto& transaction : block.transactions()) {
        CurrentTransactions.push_back(
            extractTransaction(transaction, position));
        position++;
      }
      totalTxns = position;
      // adjacencyMatrix.resize(totalTxns, vector<int>(totalTxns, 0));
      // Calculate padded size: round up totalTxns to the nearest multiple of
      size_t paddedColumns = ceil(static_cast<double>(totalTxns) / 16) * 16;
      // Resize the matrix with padding
      adjacencyMatrix.resize(totalTxns, vector<int>(paddedColumns, 0));
      inDegree = unique_ptr<atomic<int>[]>(new atomic<int>[totalTxns]);
      for (i = 0; i < totalTxns; ++i) {
        inDegree[i].store(0);  // Atomic store to set initial value to 0
      }

      for (i = 0; i < threadCount; i++) {
        threads[i] = thread(&DAGmodule::dependencyMatrix, this, i);
      }
      for (i = 0; i < threadCount; i++) {
        threads[i].join();
      }

      return true;
    } else {
      std::cerr << "Failed to parse block.proto data.\n";
    }
    return true;
  }

  // Function to select a transaction from DAG
  int selectTxn() {
    int pos, var_zero = 0;
    pos = lastTxn.load() + 1;

    for (int i = pos; i < totalTxns; i++) {
      if (inDegree[i].load() == 0) {
        if (inDegree[i].compare_exchange_strong(var_zero, -1)) {
          lastTxn.store(i);
          return i;  // Return the index if transaction is found
        }
      }
    }
    for (int i = 0; i < totalTxns; i++) {
      if (inDegree[i].load() == 0) {
        if (inDegree[i].compare_exchange_strong(var_zero, -1)) {
          lastTxn.store(i);
          return i;  // Return the index if transaction is found
        }
      }
    }

    return -1;
  }
  void complete(int txnID) {
    // Iterate over all transactions to find those dependent on txn_id
    if (txnID >= 0) {
      inDegree[txnID].fetch_sub(1);
      completedTxns++;
    } else {
      return;
    }
    
      for (int i = txnID + 1; i < totalTxns; i++) {
        if (adjacencyMatrix[txnID][i] ==
            1) {  // If there is a dependency from txn_id to j
          if (inDegree[i].load() > 0) {
            inDegree[i].fetch_sub(1);  // Decrease in-degree atomically
          }
        }
      }
  }

  void append(atomic<Node *> &head, int value)
  {
    Node *new_node = new Node(value);
    Node *old_head = head.load();
    new_node->next = old_head;
    while (!head.compare_exchange_weak(old_head, new_node))
    {
      new_node->next = old_head;
    }
  }

  bool check_edge(int lastWrite, const TransactionStruct &txn)
  {
    // Only check edge if lastWrite has been set (> -1)
    // This allows initial writes where lastWrite is still -1
    return lastWrite <= -1 || adjacencyMatrix[lastWrite][txn.txn_no]==1;
  }

  // Function to validate a single transaction
  bool validateTransaction(int txn_id, atomic<int>& txn_counter, atomic<bool>& validation_failed) {
    if (txn_id == -1) {
        return true;
    }

    TransactionStruct& txn = CurrentTransactions[txn_id];

    // Check input dependencies
    for (const auto& input : txn.inputs) {
        if (validation_failed.load()) break;
        int addr = stoi(input);
        int lastWrite = addressArray[addr]->writeID.load();
        if (!check_edge(lastWrite, txn)) {
            cerr << "Malicious block producer - input validation failed" << endl;
            validation_failed.store(true);
            return false;
        }
        append(addressArray[addr]->head, txn.txn_no);
    }

    if (validation_failed.load()) return false;

    // Check output dependencies
    for (const auto& output : txn.outputs) {
        if (validation_failed.load()) break;
        int addr = stoi(output);
        
        // Check read dependencies
        Node* current = addressArray[addr]->head.load();
        while (current != nullptr && !validation_failed.load()) {
            if (!check_edge(current->value, txn)) {
                cerr << "Malicious block producer - read validation failed" << endl;
                validation_failed.store(true);
                return false;
            }
            current = current->next;
        }

        if (validation_failed.load()) break;

        // Update write ID
        int lastWrite = addressArray[addr]->writeID.load();
        int expected = lastWrite;
        if (!addressArray[addr]->writeID.compare_exchange_strong(expected, txn.txn_no)) {
            cerr << "Malicious block producer - write conflict" << endl;
            validation_failed.store(true);
            return false;
        }

        Node* oldHead = addressArray[addr]->head.exchange(nullptr);
        while (oldHead != nullptr) {
          Node* temp = oldHead;
          oldHead = oldHead->next;
          delete temp;
        }
    }

    if (!validation_failed.load()) {
        complete(txn.txn_no);
        txn_counter.fetch_add(1);
    }

    return true;
  }

  bool checkCircular() {
    // Check if adjacency matrix is circular by verifying bottom triangular is all zeros
    int n = adjacencyMatrix.size();
    for (int i = 0; i < n; i++) {
      for (int j = 0; j <= i; j++) {
        if (adjacencyMatrix[i][j] != 0) {
          return true; 
        }
      }
    }
    return false; 
  }

  // Function to validate a block using a smart validator
  bool smartValidator() {
    // Initialize address array with unique pointers
   
    for (auto& addr : addressArray) {
        addr = std::make_unique<AddressData>();
    }
    
    atomic<int> txn_counter{0};
    atomic<bool> validation_failed{false};
    atomic<int> current_txn{-1};

    while (!validation_failed.load() && txn_counter.load() < totalTxns) {
        // Find transaction with in-degree 0 using atomic operations
        int txn_id = -1;
        for (int i = 0; i < totalTxns; i++) {
            if (inDegree[i].load() == 0) {
                int expected = 0;
                if (inDegree[i].compare_exchange_strong(expected, -1)) {
                    txn_id = i;
                    break;
                }
            }
        }

        if (!validateTransaction(txn_id, txn_counter, validation_failed)) {
            return false;
        }
    }

    return txn_counter.load() == totalTxns;
  }

  // Function to execute the validator with multiple threads
  bool executeValidator() {
    if (checkCircular()) {
        cerr << "Cannot validate: Circular dependency detected in transaction graph" << endl;
        return false;
    }

    // Initialize address array with unique pointers
    addressArray.clear();
    addressArray.resize(ADDRESS_DATA_SIZE);
    for (auto& addr : addressArray) {
        addr = std::make_unique<AddressData>();
    }

    vector<thread> threads(threadCount);
    atomic<int> txn_counter{0};
    atomic<bool> validation_failed{false};
    atomic<int> current_txn{-1};

    auto process_transactions = [this, &txn_counter, &validation_failed, &current_txn]() {
        while (!validation_failed.load() && txn_counter.load() < totalTxns) {
            // Find transaction with in-degree 0 using atomic operations
            int txn_id = -1;
            for (int i = 0; i < totalTxns; i++) {
                if (inDegree[i].load() == 0) {
                    int expected = 0;
                    if (inDegree[i].compare_exchange_strong(expected, -1)) {
                        txn_id = i;
                        break;
                    }
                }
            }

            if (txn_id != -1) {
              validateTransaction(txn_id, txn_counter, validation_failed);
          }
    
            if (txn_counter.load() == totalTxns) {
              // No more transactions available
                return;
            }    
        }
    };

    // Create and start threads
    for (int i = 0; i < threadCount; i++) {
        threads[i] = thread(process_transactions);
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Check if validation failed
    if (validation_failed.load()) {
        return false;
    }

    // Verify all transactions were processed
    return txn_counter.load() == totalTxns;
  }
};