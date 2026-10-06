// ?food (an IRI bound by R) is expanded over its incoming ex:eats edges
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(id{"http://example.org/bob"}, id{"http://example.org/eats"}, ?{@vars::@food({type = !gpm.variable_binding})})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@other({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@other}, id{"http://example.org/eats"}, ?{@vars::@food})
            %res_table = relalg.materialize %e [@vars::@food, @vars::@other] => ["food", "other"] : !subop.local_table<[col1: !db.string, col2: !db.string],["food", "other"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["food", "other"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["food", "other"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["food", "other"]>
        return
    }
}
