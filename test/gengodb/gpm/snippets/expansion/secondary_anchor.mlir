// ?who is the primary anchor, ?what (also bound by R) only filters the edges
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@p({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, ?{@vars::@p}, ?{@vars::@what})
            %res_table = relalg.materialize %e [@vars::@who, @vars::@what, @vars::@p] => ["who", "what", "p"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "p"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "p"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "p"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "p"]>
        return
    }
}
