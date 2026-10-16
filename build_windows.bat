@echo off
REM Nettoie puis compile ONIRIQUE EQ (Release). A lancer depuis ce dossier.
if exist build rmdir /s /q build
cmake -B build || goto :err
cmake --build build --config Release || goto :err
echo.
echo OK : build\OniriqueEQ_artefacts\Release\VST3\ONIRIQUE EQ.vst3
goto :eof
:err
echo.
echo ERREUR pendant la compilation, voir les messages ci-dessus.
exit /b 1
