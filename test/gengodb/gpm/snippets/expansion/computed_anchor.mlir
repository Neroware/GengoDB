// anchors computed in the query (string, double, int, long) are resolved to the
// literal nodes of the graph
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/age"}, ?{@vars::@age({type = !gpm.variable_binding})})
            %m = relalg.map %r computes : [@vars::@name({type = !variant.variant}), @vars::@rating({type = !variant.variant}), @vars::@years({type = !variant.variant}), @vars::@balance({type = !variant.variant})] (%t: !tuples.tuple){
                %nameStr = db.constant ("Bob") : !db.string
                %nameVar = variant.create_scalar %nameStr : !db.string
                %rating = arith.constant 4.75 : f64
                %ratingVar = variant.create_scalar %rating : f64
                %years = arith.constant 30 : i32
                %yearsVar = variant.create_scalar %years : i32
                %balance = arith.constant -987654321012345 : i64
                %balanceVar = variant.create_scalar %balance : i64
                tuples.return %nameVar, %ratingVar, %yearsVar, %balanceVar : !variant.variant, !variant.variant, !variant.variant, !variant.variant
            }
            %e1 = gpm.relalg.graph_expansion %m expand: [@vars::@s1({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@s1}, id{"http://example.org/name"}, ?{@vars::@name})
            %e2 = gpm.relalg.graph_expansion %e1 expand: [@vars::@s2({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@s2}, id{"http://example.org/favoriteRating"}, ?{@vars::@rating})
            %e3 = gpm.relalg.graph_expansion %e2 expand: [@vars::@s3({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@s3}, id{"http://example.org/age"}, ?{@vars::@years})
            %e4 = gpm.relalg.graph_expansion %e3 expand: [@vars::@s4({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@s4}, id{"http://example.org/accountBalance"}, ?{@vars::@balance})
            %res_table = relalg.materialize %e4 [@vars::@s1, @vars::@s2, @vars::@s3, @vars::@s4] => ["s1", "s2", "s3", "s4"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["s1", "s2", "s3", "s4"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["s1", "s2", "s3", "s4"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["s1", "s2", "s3", "s4"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["s1", "s2", "s3", "s4"]>
        return
    }
}
