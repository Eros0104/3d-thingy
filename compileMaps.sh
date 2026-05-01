for f in assets/maps/*.json; do
  ./build/level_pack "$f" "${f%.json}.evil"
done