import { useEffect, useState, useCallback, useMemo } from "react";
import { Button } from "@/components/ui/button";
import { useGraphData } from "../hooks/useGraphData";
import {
  GraphScene,
  computeCameraTarget,
  type CameraTarget,
} from "./GraphScene";
import { LevelList } from "./LevelList";
import { FilterPanel } from "./FilterPanel";
import { NodeDetailPanel } from "./NodeDetailPanel";
import { ResizeHandle } from "./ResizeHandle";
import { ErrorBoundary } from "./ErrorBoundary";
import type { GraphNode, GraphData } from "../lib/types";

/* Persist panel widths */
function loadWidth(key: string, fallback: number): number {
  try {
    const v = localStorage.getItem(key);
    if (v) return Math.max(150, Math.min(600, parseInt(v, 10)));
  } catch { /* ignore */ }
  return fallback;
}
function saveWidth(key: string, value: number) {
  try { localStorage.setItem(key, String(Math.round(value))); } catch { /* ignore */ }
}

interface GraphTabProps {
  project: string | null;
}

/* A breadcrumb in the drill-down path. qn is the container's qualified_name
 * (passed to /api/graph as `parent`); name is the segment shown to the user. */
interface Crumb {
  qn: string;
  name: string;
}

export function GraphTab({ project }: GraphTabProps) {
  const { data, loading, error, fetchGraph } = useGraphData();
  /* Drill-down path. crumbs[0] is the repo root; the last crumb is the
   * currently displayed container. */
  const [crumbs, setCrumbs] = useState<Crumb[]>([]);
  const [highlightedIds, setHighlightedIds] = useState<Set<number> | null>(null);
  const [, setSelectedPath] = useState<string | null>(null);
  const [selectedNode, setSelectedNode] = useState<GraphNode | null>(null);
  const [cameraTarget, setCameraTarget] = useState<CameraTarget | null>(null);
  const [showLabels, setShowLabels] = useState(true);
  const [leftWidth, setLeftWidth] = useState(() => loadWidth("cbm-left-w", 260));
  const [rightWidth, setRightWidth] = useState(() => loadWidth("cbm-right-w", 280));

  /* Filter state — all enabled by default */
  const [enabledLabels, setEnabledLabels] = useState<Set<string>>(new Set());
  const [enabledEdgeTypes, setEnabledEdgeTypes] = useState<Set<string>>(new Set());

  /* Initialize filters when data loads */
  useEffect(() => {
    if (!data) return;
    const labels = new Set(data.nodes.map((n) => n.label));
    const types = new Set(data.edges.map((e) => e.type));
    for (const lp of data.linked_projects ?? []) {
      for (const n of lp.nodes) labels.add(n.label);
      for (const e of lp.edges) types.add(e.type);
      for (const e of lp.cross_edges) types.add(e.type);
    }
    setEnabledLabels(labels);
    setEnabledEdgeTypes(types);
  }, [data]);

  /* Compute filtered data */
  const filteredData: GraphData | null = useMemo(() => {
    if (!data) return null;

    const nodes = data.nodes.filter((n) => enabledLabels.has(n.label));
    const nodeIds = new Set(nodes.map((n) => n.id));
    const edges = data.edges.filter(
      (e) =>
        enabledEdgeTypes.has(e.type) &&
        nodeIds.has(e.source) &&
        nodeIds.has(e.target),
    );

    const linked_projects = data.linked_projects?.map((lp) => {
      const lpNodes = lp.nodes.filter((n) => enabledLabels.has(n.label));
      const lpIds = new Set(lpNodes.map((n) => n.id));
      const lpEdges = lp.edges.filter(
        (e) =>
          enabledEdgeTypes.has(e.type) && lpIds.has(e.source) && lpIds.has(e.target),
      );
      const crossEdges = lp.cross_edges.filter(
        (e) =>
          enabledEdgeTypes.has(e.type) && nodeIds.has(e.source) && lpIds.has(e.target),
      );
      return { ...lp, nodes: lpNodes, edges: lpEdges, cross_edges: crossEdges };
    });

    return { nodes, edges, total_nodes: data.total_nodes, linked_projects };
  }, [data, enabledLabels, enabledEdgeTypes]);

  /* On project change, reset to the repo root (parent = project name). */
  useEffect(() => {
    if (project) {
      const base = project.split(/[./]/).pop() || project;
      setCrumbs([{ qn: project, name: base }]);
      fetchGraph(project, project);
      setHighlightedIds(null);
      setSelectedPath(null);
      setSelectedNode(null);
    }
  }, [project, fetchGraph]);

  const handleNodeClick = useCallback(
    (node: GraphNode) => {
      if (!filteredData || !project) return;

      /* Expandable container -> drill one level deeper. */
      if (node.expandable && node.qn) {
        const qn = node.qn;
        setCrumbs((prev) => [...prev, { qn, name: node.name }]);
        fetchGraph(project, qn);
        setHighlightedIds(null);
        setSelectedPath(null);
        setSelectedNode(null);
        setCameraTarget(null);
        return;
      }

      /* Leaf -> select + highlight its direct connections. */
      setSelectedNode(node);
      const connectedIds = new Set([node.id]);
      for (const edge of filteredData.edges) {
        if (edge.source === node.id) connectedIds.add(edge.target);
        if (edge.target === node.id) connectedIds.add(edge.source);
      }
      setHighlightedIds(connectedIds);
      setSelectedPath(node.file_path ?? null);
      setCameraTarget(computeCameraTarget(filteredData.nodes, connectedIds));
    },
    [filteredData, project, fetchGraph],
  );

  /* Jump to an ancestor in the breadcrumb path. */
  const navigateToCrumb = useCallback(
    (index: number) => {
      if (!project) return;
      const next = crumbs.slice(0, index + 1);
      const target = next[next.length - 1];
      setCrumbs(next);
      fetchGraph(project, target.qn);
      setHighlightedIds(null);
      setSelectedPath(null);
      setSelectedNode(null);
      setCameraTarget(null);
    },
    [project, crumbs, fetchGraph],
  );

  const handleNavigateToNode = useCallback(
    (node: GraphNode) => {
      handleNodeClick(node);
    },
    [handleNodeClick],
  );

  const toggleLabel = useCallback((label: string) => {
    setEnabledLabels((prev) => {
      const next = new Set(prev);
      if (next.has(label)) next.delete(label);
      else next.add(label);
      return next;
    });
  }, []);

  const toggleEdgeType = useCallback((type: string) => {
    setEnabledEdgeTypes((prev) => {
      const next = new Set(prev);
      if (next.has(type)) next.delete(type);
      else next.add(type);
      return next;
    });
  }, []);

  const enableAll = useCallback(() => {
    if (!data) return;
    const labels = new Set(data.nodes.map((n) => n.label));
    const types = new Set(data.edges.map((e) => e.type));
    for (const lp of data.linked_projects ?? []) {
      for (const n of lp.nodes) labels.add(n.label);
      for (const e of lp.edges) types.add(e.type);
      for (const e of lp.cross_edges) types.add(e.type);
    }
    setEnabledLabels(labels);
    setEnabledEdgeTypes(types);
  }, [data]);

  const disableAll = useCallback(() => {
    setEnabledLabels(new Set());
    setEnabledEdgeTypes(new Set());
  }, []);

  if (!project) {
    return (
      <div className="flex items-center justify-center h-full">
        <p className="text-white/30 text-sm">
          Select a project from the Stats tab
        </p>
      </div>
    );
  }

  if (loading) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-center">
          <div className="w-8 h-8 border-2 border-cyan-400/30 border-t-cyan-400 rounded-full animate-spin mx-auto mb-3" />
          <p className="text-white/40 text-sm">Computing layout...</p>
        </div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-center p-8">
          <p className="text-red-400 text-sm mb-2">{error}</p>
          <Button
            variant="outline"
            size="sm"
            onClick={() => fetchGraph(project, crumbs[crumbs.length - 1]?.qn ?? project)}
          >
            Retry
          </Button>
        </div>
      </div>
    );
  }

  if (!data || !filteredData || filteredData.nodes.length === 0) {
    return (
      <div className="flex items-center justify-center h-full">
        <div className="text-center">
          <p className="text-white/30 text-sm mb-3">
            {data && filteredData?.nodes.length === 0
              ? "All nodes filtered out"
              : "No nodes in this project"}
          </p>
          {data && filteredData?.nodes.length === 0 && (
            <Button size="sm" onClick={enableAll}>
              Reset Filters
            </Button>
          )}
        </div>
      </div>
    );
  }

  return (
    <div className="h-full flex">
      {/* Left sidebar — resizable */}
      <div
        className="border-r border-border/30 flex flex-col h-full bg-[#0b1920]/90 backdrop-blur-md shrink-0"
        style={{ width: leftWidth }}
      >
        <FilterPanel
          data={data}
          enabledLabels={enabledLabels}
          enabledEdgeTypes={enabledEdgeTypes}
          showLabels={showLabels}
          onToggleLabel={toggleLabel}
          onToggleEdgeType={toggleEdgeType}
          onToggleShowLabels={() => setShowLabels((v) => !v)}
          onEnableAll={enableAll}
          onDisableAll={disableAll}
        />
        <LevelList
          nodes={filteredData.nodes}
          onPick={handleNodeClick}
          selectedId={selectedNode?.id ?? null}
        />
      </div>
      <ResizeHandle
        side="left"
        onResize={(d) => {
          setLeftWidth((w) => {
            const nw = Math.max(150, Math.min(500, w + d));
            saveWidth("cbm-left-w", nw);
            return nw;
          });
        }}
      />

      {/* Graph area */}
      <div className="flex-1 relative overflow-hidden">
        <ErrorBoundary>
          <GraphScene
            data={filteredData}
            highlightedIds={highlightedIds}
            cameraTarget={cameraTarget}
            showLabels={showLabels}
            onNodeClick={handleNodeClick}
          />
        </ErrorBoundary>

        {/* Breadcrumb drill path */}
        <div className="absolute top-3 left-4 right-24 flex items-center gap-1 flex-wrap text-[11px] font-mono z-10">
          {crumbs.map((c, i) => (
            <span key={c.qn} className="flex items-center gap-1">
              {i > 0 && <span className="text-white/20">/</span>}
              <button
                onClick={() => navigateToCrumb(i)}
                disabled={i === crumbs.length - 1}
                title={c.qn}
                className={
                  i === crumbs.length - 1
                    ? "text-cyan-400 cursor-default"
                    : "text-white/45 hover:text-white/80 transition-colors"
                }
              >
                {c.name}
              </button>
            </span>
          ))}
        </div>

        {/* HUD */}
        <div className="absolute top-10 left-4 text-[11px] text-white/30 pointer-events-none font-mono">
          <p>
            {filteredData.nodes.length.toLocaleString()} groups /{" "}
            {filteredData.edges.length.toLocaleString()} links
          </p>
          <p className="text-white/25 mt-0.5">
            click a node to drill in - size = code volume
          </p>
          {highlightedIds && highlightedIds.size > 0 && (
            <p className="text-cyan-400/50 mt-0.5">{highlightedIds.size} selected</p>
          )}
        </div>

        <div className="absolute top-4 right-4 flex gap-2">
          {highlightedIds && (
            <Button
              size="sm"
              onClick={() => {
                setHighlightedIds(null);
                setSelectedPath(null);
                setSelectedNode(null);
                setCameraTarget(null);
              }}
            >
              Clear
            </Button>
          )}
          <Button
            variant="outline"
            size="sm"
            onClick={() => {
              setHighlightedIds(null);
              setSelectedPath(null);
              setSelectedNode(null);
              setCameraTarget(null);
              fetchGraph(project, crumbs[crumbs.length - 1]?.qn ?? project);
            }}
          >
            Refresh
          </Button>
        </div>
      </div>

      {/* Right detail panel — resizable */}
      {selectedNode && filteredData && (
        <>
          <ResizeHandle
            side="right"
            onResize={(d) => {
              setRightWidth((w) => {
                const nw = Math.max(200, Math.min(500, w + d));
                saveWidth("cbm-right-w", nw);
                return nw;
              });
            }}
          />
          <div
            className="border-l border-border shrink-0 h-full overflow-hidden"
            style={{ width: rightWidth, maxHeight: "100%" }}
          >
            <NodeDetailPanel
              node={selectedNode}
              allNodes={filteredData.nodes}
              allEdges={filteredData.edges}
              onClose={() => {
                setSelectedNode(null);
                setHighlightedIds(null);
                setSelectedPath(null);
              }}
              onNavigate={handleNavigateToNode}
            />
          </div>
        </>
      )}
    </div>
  );
}
