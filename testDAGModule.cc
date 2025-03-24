#include <gtest/gtest.h>

#include "DAGmodule.h"  // Include the header file for the class to be tested

// Helper function to create a mock Transaction
transaction::Transaction  CreateMockTransaction(
    const std::vector<std::string>& inputs,
    const std::vector<std::string>& outputs) {
  transaction::Transaction transaction;
  transaction::TransactionHeader* header = new transaction::TransactionHeader();

  for (const auto& input : inputs) {
    header->add_inputs(input);
  }

  for (const auto& output : outputs) {
    header->add_outputs(output);
  }

  std::string serializedHeader;
  header->SerializeToString(&serializedHeader);
  transaction.set_header(serializedHeader);

  delete header;
  return transaction;
}

TEST(DAGmoduleTest, ExtractTransactionValid) {
  // Prepare mock data
  DAGmodule dag;
  std::vector<std::string> inputs = {"input1", "input2"};
  std::vector<std::string> outputs = {"output1", "output2"};
  auto mockTx = CreateMockTransaction(inputs, outputs);

  // Extract transaction
  auto txn = dag.extractTransaction(mockTx, 1);

  // Validate extracted transaction
  EXPECT_EQ(txn.txn_no, 1);
  EXPECT_EQ(txn.inputscount, inputs.size());
  EXPECT_EQ(txn.outputcount, outputs.size());
  EXPECT_EQ(txn.inputs, inputs);
  EXPECT_EQ(txn.outputs, outputs);
}

TEST(DAGmoduleTest, DependencyMatrix) {
  DAGmodule dag;
  // Prepare mock transactions
  dag.totalTxns = 2;
  dag.CurrentTransactions = {
      {0, 1, {"A"}, 1, {"B"}},
      {1, 1, {"B"}, 1, {"C"}},
  };

  dag.adjacencyMatrix.resize(dag.totalTxns, std::vector<int>(dag.totalTxns, 0));
  dag.inDegree = std::make_unique<std::atomic<int>[]>(dag.totalTxns);
  for (int i = 0; i < dag.totalTxns; ++i) {
    dag.inDegree[i].store(0);
  }

  // Test dependency matrix calculation for a single thread (PID = 0)
  dag.dependencyMatrix(0);

  // Validate the adjacency matrix and in-degrees
  EXPECT_EQ(dag.adjacencyMatrix[0][1], 1);
  EXPECT_EQ(dag.inDegree[1].load(), 1);

  // Test dependency matrix calculation for a single thread (PID = 0)
  dag.dependencyMatrix(1);

  // Validate the adjacency matrix and in-degrees
  EXPECT_EQ(dag.adjacencyMatrix[1][0], 0);
  EXPECT_EQ(dag.inDegree[0].load(), 0);
}

TEST(DAGmoduleTest, DependencyMatrix_SingleTransaction) {
  DAGmodule dag;
  // Prepare mock transactions
  dag.totalTxns = 1;
  dag.CurrentTransactions = {
      {0, 1, {"A"}, 1, {"B"}},
  };

  dag.adjacencyMatrix.resize(dag.totalTxns, std::vector<int>(dag.totalTxns, 0));
  dag.inDegree = std::make_unique<std::atomic<int>[]>(dag.totalTxns);
  for (int i = 0; i < dag.totalTxns; ++i) {
    dag.inDegree[i].store(0);
  }

  // Test dependency matrix calculation for a single thread (PID = 0)
  dag.dependencyMatrix(0);

  // Validate the adjacency matrix and in-degrees
  EXPECT_EQ(dag.adjacencyMatrix[0][0], 0);
  // Print out adjacency matrix for single transaction case
  EXPECT_EQ(dag.inDegree[0].load(), 0);
}

TEST(AdjacencyMatrixTest, SerializationTest) {
  DAGmodule dag;
  dag.adjacencyMatrix = {{0, 1, 0}, {1, 0, 1}, {0, 1, 0}};

  std::string serializedProto = dag.serializeDAG();
  ASSERT_FALSE(serializedProto.empty());

  // Deserialize the proto
  matrix::DirectedGraph deserializedProto;
  ASSERT_TRUE(deserializedProto.ParseFromString(serializedProto));
  ASSERT_EQ(deserializedProto.num_nodes(), 3);

  // Verify adjacency matrix
  ASSERT_EQ(deserializedProto.adjacencymatrix_size(), 3);
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(deserializedProto.adjacencymatrix(i).edges_size(), 3);
    for (int j = 0; j < 3; ++j) {
      ASSERT_EQ(deserializedProto.adjacencymatrix(i).edges(j),
                dag.adjacencyMatrix[i][j]);
    }
  }
}

TEST(DAGmoduleTest, CreateBlockValid) {
  DAGmodule dag;
  // Prepare mock block
  Block block;
  auto* txn1 = block.add_transactions();
  *txn1 = CreateMockTransaction({"A"}, {"B"});

  auto* txn2 = block.add_transactions();
  *txn2 = CreateMockTransaction({"B"}, {"C"});

  std::string serializedBlock;
  block.SerializeToString(&serializedBlock);

  // Test DAG creation
  bool success = dag.create(serializedBlock);

  // Validate results
  EXPECT_TRUE(success);
  EXPECT_EQ(dag.totalTxns, 2);
  EXPECT_EQ(dag.CurrentTransactions.size(), 2);
  EXPECT_EQ(dag.adjacencyMatrix.size(), 2);
  EXPECT_EQ(dag.adjacencyMatrix[0][1], 1);
  EXPECT_EQ(dag.inDegree[1].load(), 1);
}

TEST(DAGmoduleTest, ConnectedComponents) {
  DAGmodule dag;
  // Prepare mock DAG
  dag.totalTxns = 3;
  dag.adjacencyMatrix = {{0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
  dag.cTable.Clear();

  // Test connected components
  std::string serializedComponents = dag.connectedComponents();

  // Validate results
  components::componentsTable cTable;
  cTable.ParseFromString(serializedComponents);
  EXPECT_EQ(cTable.totalcomponents(), 1);
  EXPECT_EQ(cTable.componentslist_size(), 1);
  EXPECT_EQ(cTable.componentslist(0).transactionlist_size(), 3);
}

TEST(DAGmoduleTest, SelectTxn) {
  DAGmodule dag;
  // Prepare mock DAG
  dag.totalTxns = 3;
  dag.inDegree = std::make_unique<std::atomic<int>[]>(dag.totalTxns);
  for (int i = 0; i < dag.totalTxns; ++i) {
    dag.inDegree[i].store(i == 0 ? 0
                                 : 1);  // Only the first transaction is ready
  }

  // Test transaction selection
  int selectedTxn = dag.selectTxn();

  // Validate the selected transaction
  EXPECT_EQ(selectedTxn, 0);
  EXPECT_EQ(dag.inDegree[0].load(), -1);

  // Test transaction selection
  selectedTxn = dag.selectTxn();

  // Validate the selected transaction
  EXPECT_EQ(selectedTxn, -1);

  dag.inDegree[1].fetch_sub(1);

  // Test transaction selection
  selectedTxn = dag.selectTxn();

  // Validate the selected transaction
  EXPECT_EQ(selectedTxn, 1);
}

TEST(DAGmoduleTest, CompleteTransaction) {
  DAGmodule dag;
  // Prepare mock DAG
  dag.totalTxns = 3;
  dag.adjacencyMatrix = {{0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
  dag.inDegree = std::make_unique<std::atomic<int>[]>(dag.totalTxns);
  for (int i = 0; i < dag.totalTxns; ++i) {
    dag.inDegree[i].store(i == 0 ? 0 : 1);
  }

  // Complete transaction 0
  dag.complete(0);

  // Validate the in-degree updates
  EXPECT_EQ(dag.inDegree[1].load(), 0);
  EXPECT_EQ(dag.inDegree[2].load(), 1);
  // Complete transaction 0
  dag.complete(-1);
  dag.complete(0);
  // Validate the in-degree updates
  EXPECT_EQ(dag.inDegree[1].load(), 0);
  EXPECT_EQ(dag.inDegree[2].load(), 1);
}

TEST(DAGmoduleTest, SmartValidator) {
  DAGmodule dag;
  // Prepare mock block
  Block block;
  auto* txn1 = block.add_transactions();
  *txn1 = CreateMockTransaction({"1"}, {"2"});

  auto* txn2 = block.add_transactions();
  *txn2 = CreateMockTransaction({"2"}, {"3"});

  std::string serializedBlock;
  block.SerializeToString(&serializedBlock);

  // Test DAG creation
  bool success = dag.create(serializedBlock);
  bool isValid = dag.executeValidator();
  EXPECT_TRUE(isValid);  
}

TEST(DAGmoduleTest, SmartValidator_RWR) {
  DAGmodule dag;
  // Prepare mock block
  Block block;
  auto* txn1 = block.add_transactions();
  *txn1 = CreateMockTransaction({"1"}, {});

  auto* txn2 = block.add_transactions();
  *txn2 = CreateMockTransaction({"2"}, {"1"});


    auto* txn3 = block.add_transactions();
  *txn3 = CreateMockTransaction({"2"}, {"1"});

  std::string serializedBlock;
  block.SerializeToString(&serializedBlock);

  // Test DAG creation
  bool success = dag.create(serializedBlock);
  bool isValid = dag.executeValidator();
  EXPECT_TRUE(isValid);  
}


TEST(DAGmoduleTest, SmartValidator_InvalidDAG) {
    DAGmodule dag;
    
    // Prepare mock block with valid transactions
    Block block;
    
    // Transaction 1: Writes to address "1"
    auto* txn1 = block.add_transactions();
    *txn1 = CreateMockTransaction({"2"}, {"1"});
    
    // Transaction 2: Reads from "1" and writes to "2"
    auto* txn2 = block.add_transactions();
    *txn2 = CreateMockTransaction({"1"}, {"2"});
    
    // Transaction 3: Reads from "2" and writes to "3"
    auto* txn3 = block.add_transactions();
    *txn3 = CreateMockTransaction({"2"}, {"3"});

    std::string serializedBlock;
    block.SerializeToString(&serializedBlock);

    dag.create(serializedBlock);
    // Modify adjacency matrix to create invalid dependencies
    // Make transaction 3 depend on transaction 1 (creating a cycle)
    dag.adjacencyMatrix[0][1] = 0;
    dag.inDegree[1].fetch_sub(1); // Add dependency count

    // Test smart validator with invalid DAG
    bool isValid = dag.executeValidator();
    EXPECT_FALSE(isValid);
}


int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  //  DAGmodule dag;

  return RUN_ALL_TESTS();
}