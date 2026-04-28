for f in maps/*.json; do
  ./build/level_pack "$f" "${f%.json}.evil"
done