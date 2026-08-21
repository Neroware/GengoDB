// RUN: mlir-db-opt %s -split-input-file -mlir-print-local-scope --std-query-canonicalize | FileCheck %s

//CHECK: module @querymodule attributes {relalg.query_cacheable = true, relalg.query_param_count = 2 : i64}
module @querymodule attributes {relalg.query_param_count = 2 : i64} {
  //CHECK: func.func @main(%[[ARG:.*]]: !util.ref<tuple<i32, f64>>) -> (i32, f64)
  func.func @main() -> (i32, f64) {
    //CHECK: %[[P0:.*]] = util.tupleelementptr %[[ARG]][0] : <tuple<i32, f64>> -> <i32>
    //CHECK: %[[V0:.*]] = util.load %[[P0]][] : <i32> -> i32
    %0 = arith.constant {params = [{id = 0 : i64, value = 5 : i32}]} 5 : i32
    //CHECK: %[[P1:.*]] = util.tupleelementptr %[[ARG]][1] : <tuple<i32, f64>> -> <f64>
    //CHECK: %[[V1:.*]] = util.load %[[P1]][] : <f64> -> f64
    %1 = arith.constant {params = [{id = 1 : i64, value = 3.5 : f64}]} 3.5 : f64
    //CHECK: return %[[V0]], %[[V1]] : i32, f64
    return %0, %1 : i32, f64
  }
}

// -----

//CHECK: module @querymodule attributes {relalg.query_cacheable = true}
module @querymodule {
  //CHECK: func.func @main(%{{.*}}: !util.ref<tuple<>>) -> i32
  func.func @main() -> (i32) {
    //CHECK: arith.constant 7 : i32
    %0 = arith.constant 7 : i32
    return %0 : i32
  }
}

// -----

//CHECK: module @querymodule attributes {relalg.query_cacheable = false, relalg.query_param_count = 2 : i64}
module @querymodule attributes {relalg.query_param_count = 2 : i64} {
  //CHECK: func.func @main() -> (i32, f64)
  func.func @main() -> (i32, f64) {
    //CHECK: %[[C0:.*]] = arith.constant {params = [{id = 0 : i64, value = 5 : i32}]} 5 : i32
    %0 = arith.constant {params = [{id = 0 : i64, value = 5 : i32}]} 5 : i32
    //CHECK: %[[C1:.*]] = arith.constant 3.500000e+00 : f64
    %1 = arith.constant 3.5 : f64
    //CHECK: return %[[C0]], %[[C1]] : i32, f64
    return %0, %1 : i32, f64
  }
}
