
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

class DAGmodule {
 public:
  vector<TransactionStruct> CurrentTransactions;
  vector<vector<int>> adjacencyMatrix;
  unique_ptr<std::atomic<int>[]> inDegree;
  atomic<int> completedTxns{0}, lastTxn{0};  // Global atomic counter
  int totalTxns,
      threadCount = 28;  // threadcount can be input or set based on the cores
  components::componentsTable cTable;

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
    int txnAssigned, chunk = floor(totalTxns / threadCount),
                     rem = totalTxns % threadCount;
    int start = PID * (chunk + 1), end = start + chunk;
    bool flag = false;

    for (int i = start; i <= end; i++) {
      txnAssigned = i;

      if (txnAssigned >= totalTxns) {
        return;
      }
      // Check dependencies
      for (int i = txnAssigned + 1; i < totalTxns; i++) {
        flag = false;
        // Check for input-output dependencies
        for (int j = 0; j < CurrentTransactions[txnAssigned].inputscount; j++) {
          for (int k = 0; k < CurrentTransactions[i].outputcount; k++) {
            if (CurrentTransactions[txnAssigned].inputs[j] ==
                CurrentTransactions[i].outputs[k]) {
              flag = true;  // Dependency found
            }
          }
        }

        // Check for output-input dependencies
        if (!flag) {  // Only check if the previous check did not find a
                      // dependency
          for (int j = 0; j < CurrentTransactions[txnAssigned].outputcount;
               j++) {
            for (int k = 0; k < CurrentTransactions[i].inputscount; k++) {
              if (CurrentTransactions[txnAssigned].outputs[j] ==
                  CurrentTransactions[i].inputs[k]) {
                flag = true;  // Dependency found
              }
            }
          }
        }

        // Check for output-output dependencies
        if (!flag) {  // Only check if no previous dependency was found
          for (int j = 0; j < CurrentTransactions[txnAssigned].outputcount;
               j++) {
            for (int k = 0; k < CurrentTransactions[i].outputcount; k++) {
              if (CurrentTransactions[txnAssigned].outputs[j] ==
                  CurrentTransactions[i].outputs[k]) {
                flag = true;  // Dependency found
              }
            }
          }
        }

        if (flag) {
          adjacencyMatrix[txnAssigned][i] = 1;
          inDegree[i].fetch_add(1, std::memory_order_relaxed);
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
    if (inDegree[txnID].load() >= -1) {
      for (int i = txnID + 1; i < totalTxns; i++) {
        if (adjacencyMatrix[txnID][i] ==
            1) {  // If there is a dependency from txn_id to j
          if (inDegree[i].load() > 0) {
            inDegree[i].fetch_sub(1);  // Decrease in-degree atomically
          }
        }
      }
    }
  }

  // Function to validate a block using a smart validator
  bool smartValidator(const std::string& blockData) {
    Block block;

    // Deserialize blockProtoData into block object
    if (create(blockData)) {
      // calling DAGcreate temporarily, will be replaced by smart validator
      return true;
    } else {
      std::cerr << "Failed to parse block.proto data for validation.\n";
      return false;
    }
  }
};