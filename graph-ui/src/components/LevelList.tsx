import { useMemo, useState } from "react";
import { ScrollArea } from "@/components/ui/scroll-area";
import type { GraphNode } from "../lib/types";

interface LevelListProps {
  nodes: GraphNode[];
  onPick: (node: GraphNode) => void;
  selectedId: number | null;
}

/* Aim-free navigation for the drill-down explorer: a plain clickable list of the
 * current level's container nodes. Clicking a row drills in (expandable) or
 * selects (leaf) - identical to clicking the 3D node, but without having to aim
 * at a small sphere. Sorted by code volume (descending). */
export function LevelList({ nodes, onPick, selectedId }: LevelListProps) {
  const [search, setSearch] = useState("");
  const rows = useMemo(() => {
    const q = search.trim().toLowerCase();
    return [...nodes]
      .filter((n) => !q || n.name.toLowerCase().includes(q))
      .sort((a, b) => (b.count ?? 0) - (a.count ?? 0));
  }, [nodes, search]);

  return (
    <div className="flex flex-col flex-1 min-h-0">
      <div className="px-3 py-2.5 border-b border-border/30">
        <input
          type="text"
          placeholder="Filter this level..."
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          className="w-full bg-white/[0.04] border border-white/[0.06] rounded-lg px-3 py-1.5 text-[12px] text-foreground placeholder-foreground/25 outline-none focus:border-primary/40 focus:bg-white/[0.06] transition-all"
        />
      </div>
      <ScrollArea className="flex-1 min-h-0">
        <div className="py-1">
          {rows.length === 0 ? (
            <p className="text-foreground/20 text-[12px] px-4 py-6 text-center">No matches</p>
          ) : (
            rows.map((n) => (
              <button
                key={n.id}
                onClick={() => onPick(n)}
                title={n.expandable ? `Open ${n.name}` : n.name}
                className={`flex items-center gap-2 w-full text-left px-3 py-[6px] text-[12px] transition-colors ${
                  selectedId === n.id
                    ? "bg-primary/10 text-primary"
                    : "text-foreground/65 hover:text-foreground hover:bg-white/[0.04]"
                }`}
              >
                <span
                  className="w-[8px] h-[8px] rounded-full shrink-0"
                  style={{ backgroundColor: n.color }}
                />
                <span className="truncate font-mono">{n.name}</span>
                <span className="ml-auto flex items-center gap-2 shrink-0">
                  <span className="text-foreground/25 text-[10px] tabular-nums">
                    {(n.count ?? 0).toLocaleString()}
                  </span>
                  <span className="text-primary/50 text-[12px] w-2">{n.expandable ? ">" : ""}</span>
                </span>
              </button>
            ))
          )}
        </div>
      </ScrollArea>
    </div>
  );
}
