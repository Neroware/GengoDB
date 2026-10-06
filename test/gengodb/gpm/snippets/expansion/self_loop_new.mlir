// a new variable occurring twice binds once and filters once
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(id{"http://example.org/coffee"}, ?{@vars::@p({type = !gpm.variable_binding})}, id{"http://example.org/coffee"})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@z({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@z}, ?{@vars::@p}, ?{@vars::@z})
            %res_table = relalg.materialize %e [@vars::@p, @vars::@z] => ["p", "z"] : !subop.local_table<[col1: !db.string, col2: !db.string],["p", "z"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["p", "z"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["p", "z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["p", "z"]>
        return
    }
}
