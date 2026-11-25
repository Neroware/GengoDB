// RUN: mlir-db-opt %s -split-input-file --gpm-unnest-patterns -mlir-print-local-scope | FileCheck %s

// A path is a leaf like a triple: isolated on its own named graph and joined with the group under
// binding compatibility; its bound subject ?y becomes a fresh bindings column (the join key).
//CHECK-LABEL: func.func @triple_then_path
//CHECK: %[[G1:.*]] = gpm.relalg.named_graph column : @[[S1:.*]]::@ref
//CHECK: %[[T:.*]] = gpm.relalg.triple_pattern %[[G1]] @[[S1]]::@ref(?{@vars::@x({type = !variant.variant})}, id{"http://example.org/drinks"}, ?{@vars::@y({type = !variant.variant})})
//CHECK: %[[G2:.*]] = gpm.relalg.named_graph column : @[[S2:.*]]::@ref
//CHECK: %[[P:.*]] = gpm.relalg.path %[[G2]] @[[S2]]::@ref(?{@bindings::@s({type = !variant.variant})}, ?{@vars::@z({type = !variant.variant})}) step : [id{"http://example.org/asksForMore"}] min : 1
//CHECK: relalg.join %[[T]], %[[P]]
//CHECK: tuples.getcol %{{.*}} @vars::@y
//CHECK: tuples.getcol %{{.*}} @bindings::@s
//CHECK: gpm.bindings_compatible
//CHECK: leftHash = [#tuples.columnref<@vars::@y>]{{.*}}rightHash = [#tuples.columnref<@bindings::@s>]
module {
  func.func @triple_then_path() {
    %0 = relalg.query (){
      %1 = gpm.relalg.named_graph column : @graphs::@coffee({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %4 = gpm.relalg.triple_pattern %arg0 @graphs::@coffee(?{@vars::@x({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@y({type = !gpm.variable_binding})})
        %5 = gpm.relalg.path %4 @graphs::@coffee(?{@vars::@y}, ?{@vars::@z({type = !gpm.variable_binding})}) step : [id{"http://example.org/asksForMore"}] min : 1
        tuples.return %5 : !tuples.tuplestream
      }
      %3 = relalg.materialize %2 [@vars::@x,@vars::@z] => ["x", "z"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["x", "z"]>
      relalg.query_return %3 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["x", "z"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["x", "z"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["x", "z"]>
    return
  }
}

// -----

// ?x p+ ?x: both ends stay the same column (the lowering keeps only the pairs (x, x)); no join.
//CHECK-LABEL: func.func @same_variable
//CHECK: %[[G:.*]] = gpm.relalg.named_graph column : @[[S:.*]]::@ref
//CHECK: gpm.relalg.path %[[G]] @[[S]]::@ref(?{@vars::@x({type = !variant.variant})}, ?{@vars::@x({type = !variant.variant})}) step : [id{"http://example.org/rel"}] min : 1
//CHECK-NOT: relalg.join
module {
  func.func @same_variable() {
    %0 = relalg.query (){
      %1 = gpm.relalg.named_graph column : @graphs::@coffee({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %4 = gpm.relalg.path %arg0 @graphs::@coffee(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@x}) step : [id{"http://example.org/rel"}] min : 1
        tuples.return %4 : !tuples.tuplestream
      }
      %3 = relalg.materialize %2 [@vars::@x] => ["x"] : !subop.local_table<[col1$0 : !db.string], ["x"]>
      relalg.query_return %3 : !subop.local_table<[col1$0 : !db.string], ["x"]>
    } -> !subop.local_table<[col1$0 : !db.string], ["x"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string], ["x"]>
    return
  }
}
