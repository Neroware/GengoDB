module {
  func.func @main() {
    %0 = relalg.query (){
      %1 = gpm.named_graph column : @graphs::@coffee({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
      %2 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %8 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        tuples.return %8 : !tuples.tuplestream
      }
      %3 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %8 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars_u_1::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars_u_1::@what({type = !gpm.variable_binding})})
        tuples.return %8 : !tuples.tuplestream
      }
      %4 = gpm.bag %2, %3  mapping: {@union::@who({type = !gpm.variable_binding})=[@vars::@who,@vars_u_1::@who], @union::@what({type = !gpm.variable_binding})=[@vars::@what,@vars_u_1::@what]}
      %5 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %8 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars_u_2::@who({type = !gpm.variable_binding})}, id{"http://example.org/age"}, ?{@vars_u_2::@what({type = !gpm.variable_binding})})
        tuples.return %8 : !tuples.tuplestream
      }
      %6 = gpm.bag %4, %5  mapping: {@union_u_1::@who({type = !gpm.variable_binding})=[@union::@who,@vars_u_2::@who], @union_u_1::@what({type = !gpm.variable_binding})=[@union::@what,@vars_u_2::@what]}
      %7 = relalg.materialize %6 [@union_u_1::@who,@union_u_1::@what] => ["who", "what"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
      relalg.query_return %7 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    return
  }
}
