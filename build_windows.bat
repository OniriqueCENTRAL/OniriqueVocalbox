@echo off
REM Nettoie puis compile ONIRIQUE VOCALBOX (Release). A lancer depuis ce dossier.
if exist build rmdir /s /q build
cmake -B build || goto :err
cmake --build build --config Release || goto :err
echo.
echo OK : build\OniriqueVocalBox_artefacts\Release\VST3\ONIRIQUE VOCALBOX.vst3
goto :eof
:err
echo.
echo ERREUR pendant la compilation, voir les messages ci-dessus.
exit /b 1
