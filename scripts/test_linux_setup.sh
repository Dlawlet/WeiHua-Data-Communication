#!/bin/bash
# Test de validation pour le script Linux

echo "════════════════════════════════════════════════════════════"
echo "🧪 Test de validation - Resource Limiter Linux"
echo "════════════════════════════════════════════════════════════"

# Vérifier les dépendances
echo "Vérification des dépendances..."

if ! command -v python3 &> /dev/null; then
    echo "❌ Python3 n'est pas installé"
    exit 1
fi
echo "✅ Python3 trouvé"

if ! python3 -c "import psutil" 2>/dev/null; then
    echo "⚠️  psutil non installé - installation requise:"
    echo "   pip install psutil"
fi

if ! command -v cpulimit &> /dev/null; then
    echo "⚠️  cpulimit non installé (optionnel mais recommandé)"
    echo "   sudo apt-get install cpulimit"
else
    echo "✅ cpulimit trouvé"
fi

if ! command -v bc &> /dev/null; then
    echo "⚠️  bc non installé (requis pour mesure de temps)"
    echo "   sudo apt-get install bc"
else
    echo "✅ bc trouvé"
fi

echo ""
echo "════════════════════════════════════════════════════════════"
echo "📝 Tests disponibles:"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "1. Test avec Python (multiplateforme)"
echo "   python3 scripts/resource_limiter.py ./program -i input.txt -O output.txt"
echo ""
echo "2. Test avec Bash (Linux natif)"
echo "   chmod +x scripts/run_with_limits_linux.sh"
echo "   ./scripts/run_with_limits_linux.sh ./program input.txt output.txt 30 512 50"
echo ""
echo "3. Calcul du score"
echo "   python3 compute_score.py input.txt output.txt"
echo ""
echo "════════════════════════════════════════════════════════════"

# Vérifier les permissions du script
if [ -f "scripts/run_with_limits_linux.sh" ]; then
    if [ ! -x "scripts/run_with_limits_linux.sh" ]; then
        echo "⚠️  Script Bash n'est pas exécutable - correction..."
        chmod +x scripts/run_with_limits_linux.sh
        echo "✅ Permissions corrigées"
    else
        echo "✅ Script Bash est exécutable"
    fi
fi

echo ""
echo "✅ Validation terminée - Prêt à tester!"
