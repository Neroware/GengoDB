module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.named_graph name : "file://resources/ttl/coffee.ttl#rdf", column : @graphs::@coffee({type = !gpm.graph_ref})
            %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
                %1 = gpm.triple_pattern %arg @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, _{"somefood"})
                %2 = gpm.triple_pattern %1 @graphs::@coffee(_{"somefood"}, id{"http://example.org/portions"}, ?{@vars::@amount({type = !gpm.variable_binding})})
                tuples.return %2 : !tuples.tuplestream
            }
            %res_table = relalg.materialize %bgp [@vars::@who, @vars::@amount] => ["who", "amount"] : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "amount"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "amount"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["who", "amount"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["who", "amount"]>
        return
    }
}
