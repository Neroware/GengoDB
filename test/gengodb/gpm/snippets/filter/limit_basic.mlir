module {
  func.func @main() {
    %0 = relalg.query (){
      %g0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %gpm0 = gpm.basic_graph_pattern %g0 (%arg0: !tuples.tuplestream){
        %4 = gpm.triple_pattern %arg0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
        tuples.return %4 : !tuples.tuplestream
      }
      %sorted = relalg.sort %gpm0 [(@vars::@who,asc),(@vars::@what,asc)]
      %limited = relalg.limit 5 %sorted
      %res_table = relalg.materialize %limited [@vars::@who,@vars::@what] => ["who", "what"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
      relalg.query_return %res_table : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    return
  }
}
