cmake_minimum_required(VERSION 3.16)
project(QuizletDeck LANGUAGES CXX)
enable_testing()
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(quizlet_deck QuizletDeckTest.cpp
  ${REPO_ROOT}/src/quizlet/QuizletDeckParser.cpp
  ${REPO_ROOT}/src/quizlet/QuizletSession.cpp
  ${REPO_ROOT}/src/quizlet/QuizletStudy.cpp
  ${REPO_ROOT}/src/quizlet/QuizletProgress.cpp
  ${REPO_ROOT}/src/quizlet/QuizletToday.cpp
  ${REPO_ROOT}/src/quizlet/QuizletSync.cpp)
target_include_directories(quizlet_deck PRIVATE ${REPO_ROOT}/src)
target_compile_features(quizlet_deck PRIVATE cxx_std_17)
target_compile_options(quizlet_deck PRIVATE -UNDEBUG -Wall -Wextra)
add_test(NAME quizlet_deck COMMAND quizlet_deck)
