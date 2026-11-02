module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@p({type = !gpm.variable_binding})}, id{"http://example.org/cup"}, ?{@vars::@c({type = !gpm.variable_binding})})
            %2 = gpm.relalg.node_expansion %1 @graphs::@ref(?{@vars::@c} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
            %3 = gpm.relalg.edge_expansion %2 @graphs::@ref(?{@edges::@e} -> ?{@vars::@x({type = !gpm.variable_binding})}) anchor: to_node
            %res_table = relalg.materialize %3 [@vars::@p, @vars::@c, @edges::@e, @vars::@x] => ["p", "c", "e", "x"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
        return
    }
}
