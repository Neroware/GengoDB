module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee.ttl#rdf">})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@what({type = !gpm.variable_binding})})
                tuples.return %1 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@who, @vars::@what] => ["who", "what"] : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "what"]>
        return
    }
}
