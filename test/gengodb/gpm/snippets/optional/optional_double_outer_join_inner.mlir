module {
  func.func @main() {
    %0 = relalg.query (){
      // left branch: ?person eats ?food . OPTIONAL { ?person cup ?cup }
      %1 = gpm.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"defaultGraph", "gengodb://sparql/settings/defaultGraph#rdf">})
      %2 = gpm.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@person({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !gpm.variable_binding})})
      %3 = gpm.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"defaultGraph", "gengodb://sparql/settings/defaultGraph#rdf">})
      %4 = gpm.triple_pattern %3 @graphs_u_2::@ref(?{@vars::@person}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !gpm.variable_binding})}) {bindings = {s = #tuples.columndef<@bindings::@s,!gpm.variable_binding,[#tuples.columnref<@vars::@person>]>}}
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %true = db.constant(true) : i1
        tuples.return %true : i1
      }  mapping: {@outerjoin::@cup({type = !db.nullable<!gpm.variable_binding>})=[@vars::@cup]} attributes {impl = "hash", leftHash = [#tuples.columnref<@vars::@person>], nullMatchesAll = [1 : i8], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>], useHashJoin}

      // right branch: ?person drinks ?drink . OPTIONAL { ?drink asksForMore ?more }
      %6 = gpm.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"defaultGraph", "gengodb://sparql/settings/defaultGraph#rdf">})
      %7 = gpm.triple_pattern %6 @graphs_u_3::@ref(?{@vars_u_1::@person({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars_u_1::@drink({type = !gpm.variable_binding})})
      %8 = gpm.named_graph column : @graphs_u_4::@ref({type = !gpm.graph_ref<"defaultGraph", "gengodb://sparql/settings/defaultGraph#rdf">})
      %9 = gpm.triple_pattern %8 @graphs_u_4::@ref(?{@vars_u_1::@drink}, id{"http://example.org/asksForMore"}, ?{@vars_u_1::@more({type = !gpm.variable_binding})}) {bindings = {s = #tuples.columndef<@bindings_u_1::@s,!gpm.variable_binding,[#tuples.columnref<@vars_u_1::@drink>]>}}
      %10 = relalg.outerjoin %7, %9 (%arg0: !tuples.tuple){
        %true = db.constant(true) : i1
        tuples.return %true : i1
      }  mapping: {@outerjoin_u_1::@more({type = !db.nullable<!gpm.variable_binding>})=[@vars_u_1::@more]} attributes {impl = "hash", leftHash = [#tuples.columnref<@vars_u_1::@drink>], nullMatchesAll = [1 : i8], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@s>], useHashJoin}

      // top-level inner join, combining the two OPTIONAL-bearing branches on ?person
      %11 = relalg.join %5, %10 (%arg0: !tuples.tuple){
        tuples.return
      } attributes {impl = "hash", leftHash = [#tuples.columnref<@vars::@person>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@vars_u_1::@person>], useHashJoin}

      %12 = relalg.materialize %11 [@vars::@person,@vars::@food,@outerjoin::@cup,@vars_u_1::@drink,@outerjoin_u_1::@more] => ["person", "food", "cup", "drink", "more"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["person", "food", "cup", "drink", "more"]>
      relalg.query_return %12 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["person", "food", "cup", "drink", "more"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["person", "food", "cup", "drink", "more"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["person", "food", "cup", "drink", "more"]>
    return
  }
}
