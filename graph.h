#include "model.h"

#pragma once

struct ConflictGraph {
    int n; // Количестиво вершин
    std::vector<std::vector<bool>> adj; // adj[i][j] = true, если есть ребро

    ConflictGraph(int n) : n(n), adj(n, std::vector<bool>(n, false)) {}
};

bool areConflicting(const Exam& a, const Exam& b);
ConflictGraph buildConflictGraph(const std::vector<Exam>& exams);
std::vector<int> greedyColoring(const ConflictGraph& g);