// an expansion on top of an expansion
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
            %e1 = gpm.relalg.graph_expansion %r expand: [@vars::@what({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/drinks"}, ?{@vars::@what})
            %e2 = gpm.relalg.graph_expansion %e1 expand: [@vars::@t({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@what}, id{"http://example.org/temp"}, ?{@vars::@t})
            %res_table = relalg.materialize %e2 [@vars::@who, @vars::@what, @vars::@t] => ["who", "what", "t"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "t"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "t"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "t"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "what", "t"]>
        return
    }
}
