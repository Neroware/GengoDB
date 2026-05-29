module @querymodule  {
    func.func @query() {
        // Get named graph "coffee" whose IRI is already stored in the catalog
        // %0 = gpm.basegraph graph : "coffee", column : @graphs::@coffee({type = !gpm.graph_ref})

        // Download and get the RDF graph from the semantic web at the provided IRI
        // %0 = gpm.basegraph graph : "https://raw.githubusercontent.com/Neroware/LingoDB/refs/heads/rz/main/resources/ttl/coffee.ttl#rdf", column : @graphs::@coffee({type = !gpm.graph_ref})
        
        // Load the graph from the local RDF file.
        %0 = gpm.basegraph graph : "file://resources/ttl/coffee.ttl", column : @graphs::@coffee({type = !gpm.graph_ref})
        %1 = gpm.triple_pattern %0 @graphs::@coffee(?{@vars::@who({type = !gpm.variable_binding})}, id{"ex:drinks"}, id{"ex:Coffee"})
        %res_table = relalg.materialize %1 [] => [] : !subop.local_table<[],[]>
        subop.set_result 0 %res_table : !subop.local_table<[],[]>
        return
    }
}

