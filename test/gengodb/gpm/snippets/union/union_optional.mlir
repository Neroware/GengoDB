module {
  func.func @main() {
    %0 = relalg.query (){
      %1 = gpm.named_graph column : @graphs::@coffee({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %5 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
        tuples.return %5 : !tuples.tuplestream
      }
      %3 = gpm.optional_graph_pattern %2 (%arg0: !tuples.tuplestream){
        %5 = gpm.basic_graph_pattern %arg0 (%arg1: !tuples.tuplestream){
          %8 = gpm.triple_pattern %arg1 @graphs::@coffee(?{@vars::@who}, id{"http://example.org/drinks"}, ?{@vars_u_1::@what({type = !gpm.variable_binding})})
          tuples.return %8 : !tuples.tuplestream
        }
        %6 = gpm.basic_graph_pattern %arg0 (%arg1: !tuples.tuplestream){
          %8 = gpm.triple_pattern %arg1 @graphs::@coffee(?{@vars::@who}, id{"http://example.org/age"}, ?{@vars_u_2::@age({type = !gpm.variable_binding})})
          tuples.return %8 : !tuples.tuplestream
        }
        %7 = gpm.bag %5, %6  mapping: {@union::@what({type = !db.nullable<!gpm.variable_binding>})=[@vars_u_1::@what,unit], @union::@age({type = !db.nullable<!gpm.variable_binding>})=[unit,@vars_u_2::@age]}
        tuples.return %7 : !tuples.tuplestream
      }
      %4 = relalg.materialize %3 [@vars::@who,@union::@what,@union::@age] => ["who", "what", "age"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string], ["who", "what", "age"]>
      relalg.query_return %4 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string], ["who", "what", "age"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string], ["who", "what", "age"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string], ["who", "what", "age"]>
    return
  }
}
