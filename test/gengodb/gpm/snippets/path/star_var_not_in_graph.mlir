// BIND(ex:nothere AS ?v) . ?v ex:rel* ?z  (computed anchor not in G) -> (ex:nothere, ex:nothere) (1 row)
module  {
    func.func @main() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = relalg.const_relation columns : [@c::@one({type = i32})] values : [[1]]
            %2 = relalg.map %1 computes : [@c::@v({type = !variant.variant})] (%t: !tuples.tuple) {
                %s = db.constant("http://example.org/nothere") : !db.string
                %v = variant.create_scalar %s : !db.string {type = 1002}
                tuples.return %v : !variant.variant
            }
            %out = gpm.relalg.path_expansion %2 @graphs::@ref(?{@c::@v} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://example.org/rel"}] min: 0 anchor: outgoing
            %res_table = relalg.materialize %out [@c::@v, @vars::@z] => ["v", "z"] : !subop.local_table<[col1: !db.string, col2: !db.string],["v", "z"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["v", "z"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["v", "z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["v", "z"]>
        return
    }
}
