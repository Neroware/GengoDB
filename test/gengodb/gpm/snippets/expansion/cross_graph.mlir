// R comes from graph coffee, the expansion runs over defaultGraph (same data):
// IRIs are resolved by name in the target graph
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %g2 = gpm.relalg.named_graph column : @graphs2::@ref({type = !gpm.graph_ref<"defaultGraph", "gengodb://sparql/settings/defaultGraph#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@what({type = !gpm.variable_binding})] pattern: @graphs2::@ref(?{@vars::@who}, id{"http://example.org/drinks"}, ?{@vars::@what})
            %res_table = relalg.materialize %e [@vars::@who, @vars::@what] => ["who", "what"] : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        return
    }
}
