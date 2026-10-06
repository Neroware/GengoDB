// ?who (bound by R) is expanded over its outgoing ex:eats edges
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@food({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, ?{@vars::@food})
            %res_table = relalg.materialize %e [@vars::@who, @vars::@what, @vars::@food] => ["who", "what", "food"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "food"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "food"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "food"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "food"]>
        return
    }
}
