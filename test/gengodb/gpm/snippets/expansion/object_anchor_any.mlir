// every object of ex:bob (IRIs, blank nodes and literals of every storage tier) is
// expanded over its incoming edges; literals are found through the graph's literal index
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(id{"http://example.org/bob"}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@o({type = !gpm.variable_binding})})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@other({type = !gpm.variable_binding}), @vars::@p2({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@other}, ?{@vars::@p2}, ?{@vars::@o})
            %res_table = relalg.materialize %e [@vars::@p, @vars::@o, @vars::@other, @vars::@p2] => ["p", "o", "other", "p2"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "o", "other", "p2"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "o", "other", "p2"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "o", "other", "p2"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "o", "other", "p2"]>
        return
    }
}
