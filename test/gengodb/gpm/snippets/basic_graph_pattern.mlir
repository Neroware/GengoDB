module @querymodule  {
    func.func @query() {
        %0 = gpm.basegraph graph : "file://resources/ttl/coffee.ttl", column : @graphs::@coffee({type = !gpm.graph_ref})
        %bgp = gpm.basic_graph_pattern %0 (%arg : !tuples.tuplestream){
            %1 = gpm.triple_pattern %arg @graphs::@coffee(_{"guy"}, id{"ex:drinks"}, id{"ex:Coffee"})
            %2 = gpm.triple_pattern %1 @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"ex:drinks"}, id{"ex:Coffee"})
            %3 = gpm.triple_pattern %2 @graphs::@coffee(?{@vars::@who}, id{"rdf:type"}, id{"ex:Person"})
            tuples.return %3 : !tuples.tuplestream
        }
        %res_table = relalg.materialize %bgp [] => [] : !subop.local_table<[],[]>
        subop.set_result 0 %res_table : !subop.local_table<[],[]>
        return
    }
}
