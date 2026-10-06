// R comes from graph coffee, the expansion runs over defaultGraph (same data):
// literals are resolved through the literal index of the target graph
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %g2 = gpm.relalg.named_graph column : @graphs2::@ref({type = !gpm.graph_ref<"defaultGraph", "gengodb://sparql/settings/defaultGraph#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/favoriteRating"}, ?{@vars::@rating({type = !gpm.variable_binding})})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@other({type = !gpm.variable_binding})] pattern: @graphs2::@ref(?{@vars::@other}, id{"http://example.org/favoriteRating"}, ?{@vars::@rating})
            %res_table = relalg.materialize %e [@vars::@rating, @vars::@other] => ["rating", "other"] : !subop.local_table<[col1: !db.string, col2: !db.string],["rating", "other"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["rating", "other"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["rating", "other"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["rating", "other"]>
        return
    }
}
