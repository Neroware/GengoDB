// RUN: mlir-db-opt %s -mlir-print-local-scope --relalg-parameterize | FileCheck %s

//CHECK: module @querymodule attributes {relalg.query_param_count = 1 : i64}
module @querymodule {
  %0 = relalg.basetable {table_identifier = "table1"} columns:{ col1 => @table1::@col1({type = i32}),col2 => @table1::@col2({type = i32})}
  //CHECK: relalg.selection
  //CHECK: db.constant(5{{.*}}) : i32
  //CHECK: attributes {params = [{id = 0 : i64, value = 5 : i64}]}
  %1 = relalg.selection %0 (%arg0: !tuples.tuple) {
    %2 = tuples.getcol %arg0 @table1::@col1 : i32
    %3 = db.constant(5) : i32
    %4 = db.compare eq %2 : i32, %3 : i32
    tuples.return %4 : i1
  }
  //CHECK: relalg.map
  //CHECK-NOT: params
  //CHECK: db.constant(9{{.*}}) : i32
  %5 = relalg.map %1 computes : [@map::@attr2({type = i32})] (%arg0: !tuples.tuple) {
    %6 = db.constant(9) : i32
    tuples.return %6 : i32
  }
}
