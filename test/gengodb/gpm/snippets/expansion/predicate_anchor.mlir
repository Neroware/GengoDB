// only the predicate is anchored: all edges are scanned and filtered by ?p
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(id{"http://example.org/bob"}, ?{@vars::@p({type = !gpm.variable_binding})}, id{"http://example.org/sushi"})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@s({type = !gpm.variable_binding}), @vars::@o({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@s}, ?{@vars::@p}, ?{@vars::@o})
            %res_table = relalg.materialize %e [@vars::@p, @vars::@s, @vars::@o] => ["p", "s", "o"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["p", "s", "o"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["p", "s", "o"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["p", "s", "o"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["p", "s", "o"]>
        return
    }
}
