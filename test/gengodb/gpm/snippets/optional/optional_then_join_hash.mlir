// Binding compatible inner join on a variable that is unbound on the left (from OPTIONAL), forced to the hash join.
// Unnested IR: the join carries gpm.merge, the physical join only matches the bindings.
// ?person ex:eats ?food . OPTIONAL { ?person ex:cup ?cup . } ?who ex:cup ?cup .
module {
  func.func @main() {
    %0 = relalg.query (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@person({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !variant.variant})})
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @vars::@person : !variant.variant
        %11 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        tuples.return %12 : i1
      }  mapping: {@outerjoin::@cup({type = !db.nullable<!variant.variant>})=[@vars::@cup]} attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@vars::@person>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>]}
      %6 = gpm.relalg.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %7 = gpm.relalg.triple_pattern %6 @graphs_u_3::@ref(?{@vars::@who({type = !variant.variant})}, id{"http://example.org/cup"}, ?{@bindings_u_1::@o({type = !variant.variant})})
      %8 = relalg.join %5, %7 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @outerjoin::@cup : !db.nullable<!variant.variant>
        %11 = tuples.getcol %arg0 @bindings_u_1::@o : !variant.variant
        %12 = gpm.bindings_compatible %10 : !db.nullable<!variant.variant>, %11 : !variant.variant
        tuples.return %12 : i1
      } attributes {impl = "hash", useHashJoin, bindingCompatible = [1 : i8], gpm.merge = [#tuples.columndef<@merge::@cup,!db.nullable<!variant.variant>,[#tuples.columnref<@outerjoin::@cup>, #tuples.columnref<@bindings_u_1::@o>]>], leftHash = [#tuples.columnref<@outerjoin::@cup>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@o>]}
      %9 = relalg.materialize %8 [@vars::@person,@vars::@food,@merge::@cup,@vars::@who] => ["person", "food", "cup", "who"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["person", "food", "cup", "who"]>
      relalg.query_return %9 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["person", "food", "cup", "who"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["person", "food", "cup", "who"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["person", "food", "cup", "who"]>
    return
  }
}

