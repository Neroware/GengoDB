// the predicate of the expansion is a new variable
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %g = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %r = gpm.relalg.triple_pattern %g @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, id{"http://example.org/sushi"})
            %e = gpm.relalg.graph_expansion %r expand: [@vars::@p({type = !gpm.variable_binding}), @vars::@x({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, ?{@vars::@p}, ?{@vars::@x})
            %res_table = relalg.materialize %e [@vars::@who, @vars::@p, @vars::@x] => ["who", "p", "x"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "p", "x"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "p", "x"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "p", "x"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["who", "p", "x"]>
        return
    }
}
