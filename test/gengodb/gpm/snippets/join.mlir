module {
  func.func @main() {
    %0 = relalg.query (){
      %g0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
      %gpm0 = gpm.basic_graph_pattern %g0 (%arg0: !tuples.tuplestream){
        %4 = gpm.triple_pattern %arg0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        %5 = gpm.triple_pattern %4 @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
        tuples.return %5 : !tuples.tuplestream
      }
      %g1 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
      %gpm1 = gpm.basic_graph_pattern %g1 (%arg1: !tuples.tuplestream){
        %inner0 = gpm.triple_pattern %arg1 @graphs::@ref(?{@vars::@who2({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what2({type = !gpm.variable_binding})})
        %inner1 = gpm.triple_pattern %inner0 @graphs::@ref(?{@vars::@who2}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
        tuples.return %inner1 : !tuples.tuplestream
      }
      %join = relalg.join %gpm0, %gpm1 (%arg0: !tuples.tuple){
        tuples.return
      } attributes {impl = "hash", leftHash = [#tuples.columnref<@vars::@who>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@vars::@who2>], rows = 0.0010000000000000002 : f64, useHashJoin}
      %3 = relalg.materialize %join [@vars::@who,@vars::@what] => ["who", "what"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
      relalg.query_return %3 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    return
  }
}
