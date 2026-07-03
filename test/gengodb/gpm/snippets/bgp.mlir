module {
  func.func @main() {
    %0 = relalg.query (){
      %g0 = gpm.named_graph name : "file://resources/ttl/coffee.ttl#rdf", column : @graphs::@coffee({type = !gpm.graph_ref})
      %gpm0 = gpm.basic_graph_pattern %g0 (%arg0: !tuples.tuplestream){
        %4 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        %5 = gpm.triple_pattern %4 @graphs::@coffee(?{@vars::@who}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
        tuples.return %5 : !tuples.tuplestream
      }
      %gpm1 = gpm.basic_graph_pattern %gpm0 (%arg1: !tuples.tuplestream){
        %inner0 = gpm.triple_pattern %arg1 @graphs::@coffee(?{@vars::@who2({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what2({type = !gpm.variable_binding})})
        %inner1 = gpm.triple_pattern %inner0 @graphs::@coffee(?{@vars::@who2}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
        tuples.return %inner1 : !tuples.tuplestream
      }
      %3 = relalg.materialize %gpm1 [@vars::@who,@vars::@what] => ["who", "what"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
      relalg.query_return %3 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    return
  }
}
