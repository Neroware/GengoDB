// FILTER over a variable that is only bound inside the OPTIONAL: ?age lives in a
// nullable column, so `gpm.get_binding` has to hand the Unspecified sentinel to
// `variant.cmp` for the tuples the optional pattern did not match. Comparing
// against Unspecified is null ("incomparable"), which drops exactly those tuples:
// bob (age 30) survives, steve (no ex:age triple) does not.
module {
  func.func @main() {
    %0 = relalg.query (){
      %1 = gpm.named_graph column : @graphs::@coffee({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
      %2 = gpm.basic_graph_pattern %1 (%arg0: !tuples.tuplestream){
        %6 = gpm.triple_pattern %arg0 @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@what({type = !gpm.variable_binding})})
        tuples.return %6 : !tuples.tuplestream
      }
      %3 = gpm.optional_graph_pattern %2 (%arg0: !tuples.tuplestream){
        %6 = gpm.basic_graph_pattern %arg0 (%arg1: !tuples.tuplestream){
          %7 = gpm.triple_pattern %arg1 @graphs::@coffee(?{@vars::@who}, id{"http://example.org/age"}, ?{@vars::@age({type = !gpm.variable_binding})})
          tuples.return %7 : !tuples.tuplestream
        }
        tuples.return %6 : !tuples.tuplestream
      }
      %4 = relalg.selection %3 (%arg0: !tuples.tuple){
        %6 = tuples.getcol %arg0 @vars::@age : !gpm.variable_binding
        %7 = gpm.get_binding %6 -> !variant.variant
        %c20_i64 = arith.constant 20 : i64
        %8 = variant.create_scalar %c20_i64 : i64
        %9 = variant.cmp gt %7, %8 -> !db.nullable<i1>
        tuples.return %9 : !db.nullable<i1>
      }
      %5 = relalg.materialize %4 [@vars::@who,@vars::@what] => ["who", "what"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
      relalg.query_return %5 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["who", "what"]>
    return
  }
}
