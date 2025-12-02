#include "graph.h"

bool areConflicting(const Exam& a, const Exam& b) {
    if (a.groupId == b.groupId) return true;

    if (a.teacherId == b.teacherId) return true;

    return false;
}

ConflictGraph buildConflictGraph(const std::vector<Exam>& exams) {
    int n = exams.size();
    ConflictGraph g(n);

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (areConflicting(exams[i], exams[j])) {
                g.adj[i][j] = true;
                g.adj[j][i] = true;
            }
        }
    }

    return g;
}

std::vector<int> greedyColoring(const ConflictGraph& g) {
    int n = g.n;
    std::vector<int> color(n, -1);

    for (int v = 0; v < n; ++v) {
        // отметить занятые цвета у соседей
        std::vector<bool> used(n, false); // максимум n цветов
        for (int u = 0; u < n; ++u) {
            if (g.adj[v][u] && color[u] != -1) {
                used[color[u]] = true;
            }
        }

        // найти минимальный свободный цвет
        int c = 0;
        while (c < n && used[c]) {
            ++c;
        }

        color[v] = c;
    }

    return color;
}