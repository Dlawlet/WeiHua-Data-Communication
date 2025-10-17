/*
 * Optimized UAV-to-ground allocation solver with aggressive preprocessing
 * 
 * Key optimizations:
 * - Massive preprocessing: precalculate all scoring factors, potentials, time windows
 * - Smart candidate reduction with multi-criteria filtering
 * - Fast greedy decoder with precomputed lookup tables
 * - Efficient data structures (vectors instead of maps where possible)
 * - Intelligent initial solution followed by focused local search
 * 
 * Compilation:
 *   g++ -std=c++17 -O3 -march=native -w -o uav_solver uav_solver.cpp
 * 
 * Usage:
 *   ./uav_solver < input.txt > output.txt
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <random>
#include <queue>
#include <map>
#include <set>
#include <tuple>
#include <chrono>
#include <iomanip>

using namespace std;

// ============================= STRUCTURES =============================

struct Flow {
    int f, x, y, tf;
    double s;
    int m1, n1, m2, n2;
};

struct ScheduleItem {
    int t, x, y;
    double z;
};

// Precomputed candidate information
struct CandidateInfo {
    int ux, uy;
    double potential;           // Overall potential score
    double avg_bandwidth;       // Average available bandwidth
    int distance;               // Manhattan distance
    vector<int> peak_times;     // Times with maximum bandwidth
    double total_capacity;      // Total available capacity in time window
};

// Precomputed time slot information
struct TimeSlotInfo {
    int t;
    double bandwidth;
    double delay_factor;
    double value;  // Combined value for sorting
};

// ============================= GLOBAL PREPROCESSING DATA =============================

vector<vector<vector<double>>> g_pre_bw;  // [t][x][y] -> bandwidth
vector<double> g_distance_factor;         // [distance] -> 2^(-0.1*distance)
vector<double> g_delay_factor;            // [delay] -> 10/(delay+10)

// Per-flow preprocessed data
vector<vector<CandidateInfo>> g_candidates;           // [flow_idx][candidate_idx]
vector<vector<vector<TimeSlotInfo>>> g_time_slots;    // [flow_idx][candidate_idx][slot_idx]

// ============================= INPUT =============================

void read_input(int& M, int& N, int& FN, int& T,
                vector<vector<double>>& uav_b,
                vector<vector<int>>& uav_phi,
                vector<Flow>& flows) {
    cin >> M >> N >> FN >> T;
    
    uav_b.assign(M, vector<double>(N, 0.0));
    uav_phi.assign(M, vector<int>(N, 0));
    
    for (int i = 0; i < M * N; i++) {
        int x, y, ph;
        double b;
        cin >> x >> y >> b >> ph;
        uav_b[x][y] = b;
        uav_phi[x][y] = ph;
    }
    
    flows.resize(FN);
    for (int i = 0; i < FN; i++) {
        cin >> flows[i].f >> flows[i].x >> flows[i].y >> flows[i].tf
            >> flows[i].s >> flows[i].m1 >> flows[i].n1 >> flows[i].m2 >> flows[i].n2;
    }
}

// ============================= AGGRESSIVE PREPROCESSING =============================

void build_bandwidth_matrix(int M, int N, int T,
                            const vector<vector<double>>& uav_b,
                            const vector<vector<int>>& uav_phi) {
    g_pre_bw.assign(T, vector<vector<double>>(M, vector<double>(N, 0.0)));
    
    for (int t = 0; t < T; t++) {
        for (int x = 0; x < M; x++) {
            for (int y = 0; y < N; y++) {
                int phi = uav_phi[x][y];
                int tau = (phi + t) % 10;
                double bw = 0.0;
                
                if (tau >= 3 && tau <= 6) {
                    bw = uav_b[x][y];
                } else if (tau == 2 || tau == 7) {
                    bw = uav_b[x][y] / 2.0;
                }
                g_pre_bw[t][x][y] = bw;
            }
        }
    }
}

void build_lookup_tables(int max_distance, int max_time) {
    // Precompute distance factors
    g_distance_factor.resize(max_distance + 1);
    for (int d = 0; d <= max_distance; d++) {
        g_distance_factor[d] = pow(2.0, -0.1 * d);
    }
    
    // Precompute delay factors
    g_delay_factor.resize(max_time + 1);
    for (int dt = 0; dt <= max_time; dt++) {
        g_delay_factor[dt] = 10.0 / (dt + 10.0);
    }
}

// Build smart candidates with aggressive filtering
void build_smart_candidates(const vector<Flow>& flows, int T, int M, int N, int max_time_window = 60) {
    int FN = flows.size();
    g_candidates.resize(FN);
    g_time_slots.resize(FN);
    
    for (int idx = 0; idx < FN; idx++) {
        const Flow& fl = flows[idx];
        vector<CandidateInfo> raw_candidates;
        
        // Step 1: Generate all possible candidates in the rectangle
        for (int ux = fl.m1; ux <= fl.m2; ux++) {
            for (int uy = fl.n1; uy <= fl.n2; uy++) {
                CandidateInfo cand;
                cand.ux = ux;
                cand.uy = uy;
                cand.distance = abs(fl.x - ux) + abs(fl.y - uy);
                
                // Calculate metrics for this candidate
                double total_bw = 0.0;
                double weighted_bw = 0.0;
                int count = 0;
                double max_bw = 0.0;
                
                int t_end = min(T, fl.tf + max_time_window);
                
                for (int t = fl.tf; t < t_end; t++) {
                    double bw = g_pre_bw[t][ux][uy];
                    if (bw > 1e-9) {
                        int dt = t - fl.tf;
                        int delay_idx = min(dt, (int)g_delay_factor.size()-1);
                        int dist_idx = min(cand.distance, (int)g_distance_factor.size()-1);
                        
                        double delay_f = g_delay_factor[delay_idx];
                        double dist_f = g_distance_factor[dist_idx];
                        
                        total_bw += bw;
                        weighted_bw += bw * delay_f * dist_f;
                        count++;
                        
                        if (bw > max_bw) {
                            max_bw = bw;
                            cand.peak_times.clear();
                            cand.peak_times.push_back(t);
                        } else if (abs(bw - max_bw) < 1e-9) {
                            cand.peak_times.push_back(t);
                        }
                    }
                }
                
                cand.total_capacity = total_bw;
                cand.avg_bandwidth = (count > 0) ? (total_bw / count) : 0.0;
                
                // Multi-criteria potential score
                double distance_penalty = cand.distance * 0.5;
                double capacity_score = (cand.total_capacity > 1e-9) ? log(1 + cand.total_capacity) : 0.0;
                double quality_score = weighted_bw;
                
                cand.potential = quality_score * 0.6 + capacity_score * 0.3 - distance_penalty * 0.1;
                
                // Only keep candidates with reasonable capacity
                if (cand.total_capacity > fl.s * 0.05 || cand.distance <= 2) {
                    raw_candidates.push_back(cand);
                }
            }
        }
        
        // Step 2: Sort by potential and keep top candidates
        sort(raw_candidates.begin(), raw_candidates.end(),
             [](const CandidateInfo& a, const CandidateInfo& b) {
                 return a.potential > b.potential;
             });
        
        // Adaptive top-K based on flow size and rectangle size
        int rect_size = (fl.m2 - fl.m1 + 1) * (fl.n2 - fl.n1 + 1);
        int top_k = min({8, rect_size, (int)raw_candidates.size()});
        top_k = max(2, top_k);
        
        // Step 3: Ensure diversity - don't take too many close candidates
        vector<CandidateInfo> final_candidates;
        final_candidates.push_back(raw_candidates[0]);
        
        for (size_t i = 1; i < raw_candidates.size() && (int)final_candidates.size() < top_k; i++) {
            bool too_close = false;
            for (const auto& fc : final_candidates) {
                int dist = abs(raw_candidates[i].ux - fc.ux) + abs(raw_candidates[i].uy - fc.uy);
                if (dist <= 1 && raw_candidates[i].potential < fc.potential * 0.8) {
                    too_close = true;
                    break;
                }
            }
            if (!too_close) {
                final_candidates.push_back(raw_candidates[i]);
            }
        }
        
        g_candidates[idx] = final_candidates;
        
        // Step 4: Precompute time slot information for each candidate
        g_time_slots[idx].resize(final_candidates.size());
        
        for (size_t ci = 0; ci < final_candidates.size(); ci++) {
            const auto& cand = final_candidates[ci];
            vector<TimeSlotInfo> slots;
            
            int t_end = min(T, fl.tf + max_time_window);
            for (int t = fl.tf; t < t_end; t++) {
                double bw = g_pre_bw[t][cand.ux][cand.uy];
                if (bw > 1e-9) {
                    TimeSlotInfo slot;
                    slot.t = t;
                    slot.bandwidth = bw;
                    int dt = t - fl.tf;
                    int delay_idx = min(dt, (int)g_delay_factor.size()-1);
                    int dist_idx = min(cand.distance, (int)g_distance_factor.size()-1);
                    
                    slot.delay_factor = g_delay_factor[delay_idx];
                    double dist_f = g_distance_factor[dist_idx];
                    slot.value = slot.delay_factor * bw * dist_f;
                    slots.push_back(slot);
                }
            }
            
            // Sort slots by value (best first)
            sort(slots.begin(), slots.end(),
                 [](const TimeSlotInfo& a, const TimeSlotInfo& b) {
                     return a.value > b.value;
                 });
            
            g_time_slots[idx][ci] = slots;
        }
    }
}

// ============================= FAST SCORING =============================

double compute_flow_score_fast(const Flow& flow, const vector<ScheduleItem>& schedule) {
    if (flow.s <= 1e-9) return 0.0;
    
    double transmitted = 0.0;
    double delay_sum = 0.0;
    double dist_sum = 0.0;
    set<pair<int,int>> landing_uavs;
    
    for (const auto& item : schedule) {
        transmitted += item.z;
        int delay = item.t - flow.tf;
        int hops = abs(flow.x - item.x) + abs(flow.y - item.y);
        
        double delay_f = g_delay_factor[min(delay, (int)g_delay_factor.size()-1)];
        double dist_f = g_distance_factor[min(hops, (int)g_distance_factor.size()-1)];
        
        delay_sum += delay_f * (item.z / flow.s);
        dist_sum += (item.z / flow.s) * dist_f;
        landing_uavs.insert({item.x, item.y});
    }
    
    double u2g = min(transmitted / flow.s, 1.0);
    int k = landing_uavs.empty() ? 1 : landing_uavs.size();
    double land = 1.0 / k;
    
    return 100.0 * (0.4 * u2g + 0.2 * delay_sum + 0.3 * dist_sum + 0.1 * land);
}

// ============================= ULTRA-FAST GREEDY DECODER =============================

pair<vector<vector<ScheduleItem>>, double> greedy_allocate(
    const vector<Flow>& flows,
    const vector<int>& solution) {  // solution[i] = candidate index for flow i
    
    int FN = flows.size();
    
    // Fast capacity tracking using flat array instead of map
    vector<double> remaining_capacity(g_pre_bw.size() * g_pre_bw[0].size() * g_pre_bw[0][0].size());
    auto capacity_idx = [&](int t, int x, int y) -> int {
        return t * g_pre_bw[0].size() * g_pre_bw[0][0].size() + x * g_pre_bw[0][0].size() + y;
    };
    
    // Initialize capacities
    for (size_t t = 0; t < g_pre_bw.size(); t++) {
        for (size_t x = 0; x < g_pre_bw[0].size(); x++) {
            for (size_t y = 0; y < g_pre_bw[0][0].size(); y++) {
                remaining_capacity[capacity_idx(t, x, y)] = g_pre_bw[t][x][y];
            }
        }
    }
    
    vector<vector<ScheduleItem>> schedules(FN);
    
    // Sort flows by start time then by size
    vector<int> order(FN);
    for (int i = 0; i < FN; i++) order[i] = i;
    sort(order.begin(), order.end(), [&](int a, int b) {
        if (flows[a].tf != flows[b].tf) return flows[a].tf < flows[b].tf;
        return flows[a].s > flows[b].s;
    });
    
    for (int idx : order) {
        const Flow& fl = flows[idx];
        int cand_idx = solution[idx];
        
        if (cand_idx >= (int)g_candidates[idx].size()) {
            cand_idx = 0;
        }
        
        const auto& cand = g_candidates[idx][cand_idx];
        const auto& slots = g_time_slots[idx][cand_idx];
        
        double remaining_need = fl.s;
        
        // Allocate from precomputed sorted slots
        for (const auto& slot : slots) {
            if (remaining_need <= 1e-9) break;
            
            int cidx = capacity_idx(slot.t, cand.ux, cand.uy);
            double avail = remaining_capacity[cidx];
            
            if (avail > 1e-9) {
                double use = min(avail, remaining_need);
                remaining_capacity[cidx] -= use;
                remaining_need -= use;
                schedules[idx].push_back({slot.t, cand.ux, cand.uy, use});
            }
        }
        
        // If still need more, try second best candidate
        if (remaining_need > fl.s * 0.1 && g_candidates[idx].size() > 1) {
            int cand_idx2 = (cand_idx + 1) % g_candidates[idx].size();
            const auto& cand2 = g_candidates[idx][cand_idx2];
            const auto& slots2 = g_time_slots[idx][cand_idx2];
            
            for (const auto& slot : slots2) {
                if (remaining_need <= 1e-9) break;
                
                int cidx = capacity_idx(slot.t, cand2.ux, cand2.uy);
                double avail = remaining_capacity[cidx];
                
                if (avail > 1e-9) {
                    double use = min(avail, remaining_need);
                    remaining_capacity[cidx] -= use;
                    remaining_need -= use;
                    schedules[idx].push_back({slot.t, cand2.ux, cand2.uy, use});
                }
            }
        }
    }
    
    // Compute total score
    double total_s = 0.0;
    for (const auto& fl : flows) total_s += fl.s;
    
    double weighted = 0.0;
    for (int i = 0; i < FN; i++) {
        double sc = compute_flow_score_fast(flows[i], schedules[i]);
        weighted += sc * flows[i].s;
    }
    
    double total_score = weighted / (total_s + 1e-12);
    return {schedules, total_score};
}

// ============================= SMART INITIALIZATION =============================

vector<int> generate_smart_initial_solution(const vector<Flow>& flows) {
    int FN = flows.size();
    vector<int> solution(FN);
    
    // For each flow, pick the best candidate (index 0, since they're sorted by potential)
    for (int i = 0; i < FN; i++) {
        solution[i] = 0;
    }
    
    return solution;
}

// ============================= LOCAL SEARCH =============================

// Cache pour éviter de réévaluer les mêmes solutions
struct SolutionHash {
    size_t operator()(const vector<int>& v) const {
        size_t h = 0;
        for (int i = 0; i < min(20, (int)v.size()); i++) {
            h ^= v[i] + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

pair<vector<int>, double> local_search(const vector<Flow>& flows,
                                       vector<int> solution,
                                       double current_score,
                                       int max_iterations = 100) {
    int FN = flows.size();
    mt19937 rng(42);
    
    double best_score = current_score;
    vector<int> best_solution = solution;
    
    int no_improve = 0;
    
    // Identify flows with poor allocation (transmitted < 80% of required)
    auto [schedules_cur, _] = greedy_allocate(flows, solution);
    vector<int> problematic_flows;
    for (int i = 0; i < FN; i++) {
        double transmitted = 0.0;
        for (const auto& item : schedules_cur[i]) {
            transmitted += item.z;
        }
        if (transmitted < flows[i].s * 0.8) {
            problematic_flows.push_back(i);
        }
    }
    
    for (int iter = 0; iter < max_iterations; iter++) {
        vector<int> new_solution = solution;
        
        // Focus on problematic flows 70% of the time
        if (!problematic_flows.empty() && (rng() % 10) < 7) {
            int idx = problematic_flows[rng() % problematic_flows.size()];
            int num_cands = g_candidates[idx].size();
            if (num_cands > 1) {
                new_solution[idx] = rng() % num_cands;
            }
        } else {
            // Random exploration
            int num_changes = (rng() % 2) + 1;
            for (int c = 0; c < num_changes; c++) {
                int flow_idx = rng() % FN;
                int num_cands = g_candidates[flow_idx].size();
                if (num_cands > 1) {
                    new_solution[flow_idx] = rng() % num_cands;
                }
            }
        }
        
        auto [schedules, score] = greedy_allocate(flows, new_solution);
        
        if (score > best_score + 1e-9) {
            best_score = score;
            best_solution = new_solution;
            solution = new_solution;
            no_improve = 0;
            
            // Update problematic flows list
            problematic_flows.clear();
            for (int i = 0; i < FN; i++) {
                double transmitted = 0.0;
                for (const auto& item : schedules[i]) {
                    transmitted += item.z;
                }
                if (transmitted < flows[i].s * 0.8) {
                    problematic_flows.push_back(i);
                }
            }
        } else {
            no_improve++;
        }
        
        if (no_improve > 20) break;
    }
    
    return {best_solution, best_score};
}

// ============================= MAIN SOLVER =============================

void solve(int M, int N, int FN, int T,
           const vector<vector<double>>& uav_b,
           const vector<vector<int>>& uav_phi,
           const vector<Flow>& flows) {
    
    // Phase 1: Aggressive preprocessing
    build_bandwidth_matrix(M, N, T, uav_b, uav_phi);
    build_lookup_tables(M + N, T);
    build_smart_candidates(flows, T, M, N, 60);
    
    // Phase 2: Smart initial solution
    auto solution = generate_smart_initial_solution(flows);
    auto [schedules_init, score_init] = greedy_allocate(flows, solution);
    
    // Phase 3: Fast local search
    auto [best_solution, best_score] = local_search(flows, solution, score_init, 150);
    auto [best_schedules, final_score] = greedy_allocate(flows, best_solution);
    
    // Output
    vector<Flow> flows_sorted = flows;
    sort(flows_sorted.begin(), flows_sorted.end(),
         [](const Flow& a, const Flow& b) { return a.f < b.f; });
    
    for (const auto& fl : flows_sorted) {
        int flow_idx = &fl - &flows_sorted[0];
        
        // Find original index
        for (int i = 0; i < FN; i++) {
            if (flows[i].f == fl.f) {
                flow_idx = i;
                break;
            }
        }
        
        const auto& sched = best_schedules[flow_idx];
        
        // Combine items with same (t, x, y)
        map<tuple<int,int,int>, double> combined;
        for (const auto& item : sched) {
            combined[{item.t, item.x, item.y}] += item.z;
        }
        
        vector<tuple<int,int,int,double>> items;
        for (const auto& [key, z] : combined) {
            items.push_back(make_tuple(get<0>(key), get<1>(key), get<2>(key), z));
        }
        sort(items.begin(), items.end());
        
        cout << fl.f << " " << items.size() << "\n";
        for (auto [t, x, y, z] : items) {
            cout << t << " " << x << " " << y << " ";
            if (abs(z - round(z)) < 1e-9) {
                cout << (int)round(z);
            } else {
                cout << fixed << setprecision(6) << z;
            }
            cout << "\n";
        }
    }
}

// ============================= MAIN =============================

int main(int argc, char* argv[]) {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);
    
    // Support optional input file argument
    if (argc > 1) {
        freopen(argv[1], "r", stdin);
    }
    
    int M, N, FN, T;
    vector<vector<double>> uav_b;
    vector<vector<int>> uav_phi;
    vector<Flow> flows;
    
    read_input(M, N, FN, T, uav_b, uav_phi, flows);
    solve(M, N, FN, T, uav_b, uav_phi, flows);
    
    return 0;
}
