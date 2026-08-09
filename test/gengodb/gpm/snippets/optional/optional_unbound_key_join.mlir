module {
  func.func @main() {
    %0 = relalg.query (){
      %1 = gpm.named_graph column : @graphs::@coffee({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %7 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@what({type = !gpm.variable_binding})})
        tuples.return %7 : !tuples.tuplestream
      }
      %3 = gpm.optional_graph_pattern %2 (%arg0: !tuples.tuplestream){
        %7 = gpm.basic_graph_pattern %arg0 (%arg1: !tuples.tuplestream){
          %8 = gpm.triple_pattern %arg1 @graphs::@coffee(?{@vars::@who}, id{"http://example.org/age"}, ?{@vars::@age({type = !gpm.variable_binding})})
          tuples.return %8 : !tuples.tuplestream
        }
        tuples.return %7 : !tuples.tuplestream
      }
      %4 = gpm.basic_graph_pattern %3 (%arg0: !tuples.tuplestream){
        %7 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars::@who2({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@what2({type = !gpm.variable_binding})})
        tuples.return %7 : !tuples.tuplestream
      }
      %5 = gpm.optional_graph_pattern %4 (%arg0: !tuples.tuplestream){
        %7 = gpm.basic_graph_pattern %arg0 (%arg1: !tuples.tuplestream){
          %8 = gpm.triple_pattern %arg1 @graphs::@coffee(?{@vars::@who2}, id{"http://example.org/age"}, ?{@vars::@age})
          tuples.return %8 : !tuples.tuplestream
        }
        tuples.return %7 : !tuples.tuplestream
      }
      %6 = relalg.materialize %5 [@vars::@who,@vars::@what,@vars::@age,@vars::@who2,@vars::@what2] => ["who", "what", "age", "who2", "what2"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
      relalg.query_return %6 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
    return
  }
}
