#!/bin/bash
# Script pour exécuter un programme avec des limites strictes sous Linux
# Utilise ulimit, nice, et optionnellement cpulimit/cgroups

set -e

# Arguments
PROGRAM="$1"
INPUT_FILE="$2"
OUTPUT_FILE="$3"
TIMEOUT="${4:-30}"           # Timeout en secondes (défaut: 30)
MAX_MEMORY_MB="${5:-512}"    # Mémoire max en MB (défaut: 512)
MAX_CPU_PERCENT="${6:-100}"  # CPU max en % (défaut: 100)

if [ -z "$PROGRAM" ] || [ -z "$INPUT_FILE" ]; then
    echo "Usage: $0 <program> <input_file> [output_file] [timeout] [max_memory_mb] [max_cpu_percent]"
    echo ""
    echo "Examples:"
    echo "  $0 ./program input.txt output.txt 30 512 50"
    echo "  $0 ./program input.txt - 60 1024 100"
    exit 1
fi

echo "════════════════════════════════════════════════════════════"
echo "🐧 Linux Resource Limiter"
echo "════════════════════════════════════════════════════════════"
echo "📦 Program: $PROGRAM"
echo "📥 Input: $INPUT_FILE"
echo "📤 Output: ${OUTPUT_FILE:-stdout}"
echo "⏱️  Timeout: ${TIMEOUT}s"
echo "💾 Max Memory: ${MAX_MEMORY_MB} MB"
echo "🖥️  Max CPU: ${MAX_CPU_PERCENT}%"
echo "════════════════════════════════════════════════════════════"

# Convertir MB en KB pour ulimit
MAX_MEMORY_KB=$((MAX_MEMORY_MB * 1024))

# Fonction pour exécuter avec les limites
run_with_limits() {
    # Appliquer les limites ulimit
    ulimit -v $MAX_MEMORY_KB      # Mémoire virtuelle max
    ulimit -m $MAX_MEMORY_KB      # Mémoire résidente max
    ulimit -t $TIMEOUT            # Temps CPU max
    
    # Réduire la priorité si CPU limité
    if [ "$MAX_CPU_PERCENT" -lt 50 ]; then
        NICE_VALUE=10
    elif [ "$MAX_CPU_PERCENT" -lt 80 ]; then
        NICE_VALUE=5
    else
        NICE_VALUE=0
    fi
    
    # Mesurer le temps
    START_TIME=$(date +%s.%N)
    
    # Exécuter le programme
    if [ "$OUTPUT_FILE" = "-" ] || [ -z "$OUTPUT_FILE" ]; then
        # Sortie vers stdout
        nice -n $NICE_VALUE timeout ${TIMEOUT}s "$PROGRAM" < "$INPUT_FILE"
    else
        # Sortie vers fichier
        nice -n $NICE_VALUE timeout ${TIMEOUT}s "$PROGRAM" < "$INPUT_FILE" > "$OUTPUT_FILE"
    fi
    
    EXIT_CODE=$?
    END_TIME=$(date +%s.%N)
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)
    
    echo ""
    echo "════════════════════════════════════════════════════════════"
    if [ $EXIT_CODE -eq 0 ]; then
        echo "✅ SUCCESS"
    elif [ $EXIT_CODE -eq 124 ]; then
        echo "⏰ TIMEOUT after ${TIMEOUT}s"
    elif [ $EXIT_CODE -eq 137 ]; then
        echo "💥 KILLED (possibly memory limit exceeded)"
    else
        echo "❌ FAILED with exit code $EXIT_CODE"
    fi
    echo "⏱️  Execution Time: ${ELAPSED}s"
    echo "════════════════════════════════════════════════════════════"
    
    return $EXIT_CODE
}

# Vérifier si cpulimit est disponible pour une vraie limitation CPU
if command -v cpulimit &> /dev/null && [ "$MAX_CPU_PERCENT" -lt 100 ]; then
    echo "🔧 Using cpulimit for strict CPU limiting..."
    
    # Lancer le programme en arrière-plan
    if [ "$OUTPUT_FILE" = "-" ] || [ -z "$OUTPUT_FILE" ]; then
        timeout ${TIMEOUT}s "$PROGRAM" < "$INPUT_FILE" &
    else
        timeout ${TIMEOUT}s "$PROGRAM" < "$INPUT_FILE" > "$OUTPUT_FILE" &
    fi
    
    PROGRAM_PID=$!
    
    # Appliquer cpulimit
    cpulimit -p $PROGRAM_PID -l $MAX_CPU_PERCENT -z &
    CPULIMIT_PID=$!
    
    # Attendre la fin
    wait $PROGRAM_PID
    EXIT_CODE=$?
    
    # Tuer cpulimit
    kill $CPULIMIT_PID 2>/dev/null || true
    
    exit $EXIT_CODE
else
    # Utilisation standard avec ulimit et nice
    run_with_limits
fi
