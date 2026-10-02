// Deep lexical shadowing regression: each scope reuses the same local names.
// The unwind checks verify outer scalar, struct, and array slots stay intact.
struct Box { int item; };

int main() {
	int value = 0;
	Box record = {0};
	int data[2][2] = {{0, 1}, {2, 3}};
	{
		int value = 1;
		{
			int value = 2;
			{
				int value = 3;
				int data[2][2] = {{3, 4}, {5, 6}};
				{
					int value = 4;
					Box record = {4};
					{
						int value = 5;
						{
							int value = 6;
							int data[2][2] = {{6, 7}, {8, 9}};
							{
								int value = 7;
								{
									int value = 8;
									Box record = {8};
									{
										int value = 9;
										int data[2][2] = {{9, 10}, {11, 12}};
										{
											int value = 10;
											{
												int value = 11;
												{
													int value = 12;
													Box record = {12};
													int data[2][2] = {{12, 13}, {14, 15}};
													{
														int value = 13;
														{
															int value = 14;
															{
																int value = 15;
																int data[2][2] = {{15, 16}, {17, 18}};
																{
																	int value = 16;
																	Box record = {16};
																	{
																		int value = 17;
																		{
																			int value = 18;
																			int data[2][2] = {{18, 19}, {20, 21}};
																			{
																				int value = 19;
																				{
																					int value = 20;
																					Box record = {20};
																					{
																						int value = 21;
																						int data[2][2] = {{21, 22}, {23, 24}};
																						{
																							int value = 22;
																							{
																								int value = 23;
																								{
																									int value = 24;
																									Box record = {24};
																									int data[2][2] = {{24, 25}, {26, 27}};
																									{
																										int value = 25;
																										{
																											int value = 26;
																											{
																												int value = 27;
																												int data[2][2] = {{27, 28}, {29, 30}};
																												{
																													int value = 28;
																													Box record = {28};
																													{
																														int value = 29;
																														{
																															int value = 30;
																															int data[2][2] = {{30, 31}, {32, 33}};
																															{
																																int value = 31;
																																{
																																	int value = 32;
																																	Box record = {32};
																																	{
																																		int value = 33;
																																		int data[2][2] = {{33, 34}, {35, 36}};
																																		{
																																			int value = 34;
																																			{
																																				int value = 35;
																																				{
																																					int value = 36;
																																					Box record = {36};
																																					int data[2][2] = {{36, 37}, {38, 39}};
																																					{
																																						int value = 37;
																																						{
																																							int value = 38;
																																							{
																																								int value = 39;
																																								int data[2][2] = {{39, 40}, {41, 42}};
																																								{
																																									int value = 40;
																																									Box record = {40};
																																									{
																																										int value = 41;
																																										{
																																											int value = 42;
																																											int data[2][2] = {{42, 43}, {44, 45}};
																																											{
																																												int value = 43;
																																												{
																																													int value = 44;
																																													Box record = {44};
																																													{
																																														int value = 45;
																																														int data[2][2] = {{45, 46}, {47, 48}};
																																														{
																																															int value = 46;
																																															{
																																																int value = 47;
																																																{
																																																	int value = 48;
																																																	Box record = {48};
																																																	int data[2][2] = {{48, 49}, {50, 51}};
																																																	{
																																																		int value = 49;
																																																		{
																																																			int value = 50;
																																																			{
																																																				int value = 51;
																																																				int data[2][2] = {{51, 52}, {53, 54}};
																																																				{
																																																					int value = 52;
																																																					Box record = {52};
																																																					{
																																																						int value = 53;
																																																						{
																																																							int value = 54;
																																																							int data[2][2] = {{54, 55}, {56, 57}};
																																																							{
																																																								int value = 55;
																																																								{
																																																									int value = 56;
																																																									Box record = {56};
																																																									{
																																																										int value = 57;
																																																										int data[2][2] = {{57, 58}, {59, 60}};
																																																										{
																																																											int value = 58;
																																																											{
																																																												int value = 59;
																																																												{
																																																													int value = 60;
																																																													Box record = {60};
																																																													int data[2][2] = {{60, 61}, {62, 63}};
																																																													{
																																																														int value = 61;
																																																														{
																																																															int value = 62;
																																																															{
																																																																int value = 63;
																																																																int data[2][2] = {{63, 64}, {65, 66}};
																																																																{
																																																																	int value = 64;
																																																																	Box record = {64};
																																																																	if (value != 64) return 1;
																																																																	if (record.item != 64) return 2;
																																																																	if (data[1][1] != 66) return 3;
																																																																}
	if (value != 63) return 4;
	if (record.item != 60) return 5;
	if (data[1][1] != 66) return 6;
																																																															}
	if (value != 62) return 4;
	if (record.item != 60) return 5;
	if (data[1][1] != 63) return 6;
																																																														}
	if (value != 61) return 4;
	if (record.item != 60) return 5;
	if (data[1][1] != 63) return 6;
																																																													}
	if (value != 60) return 4;
	if (record.item != 60) return 5;
	if (data[1][1] != 63) return 6;
																																																												}
	if (value != 59) return 4;
	if (record.item != 56) return 5;
	if (data[1][1] != 60) return 6;
																																																											}
	if (value != 58) return 4;
	if (record.item != 56) return 5;
	if (data[1][1] != 60) return 6;
																																																										}
	if (value != 57) return 4;
	if (record.item != 56) return 5;
	if (data[1][1] != 60) return 6;
																																																									}
	if (value != 56) return 4;
	if (record.item != 56) return 5;
	if (data[1][1] != 57) return 6;
																																																								}
	if (value != 55) return 4;
	if (record.item != 52) return 5;
	if (data[1][1] != 57) return 6;
																																																							}
	if (value != 54) return 4;
	if (record.item != 52) return 5;
	if (data[1][1] != 57) return 6;
																																																						}
	if (value != 53) return 4;
	if (record.item != 52) return 5;
	if (data[1][1] != 54) return 6;
																																																					}
	if (value != 52) return 4;
	if (record.item != 52) return 5;
	if (data[1][1] != 54) return 6;
																																																				}
	if (value != 51) return 4;
	if (record.item != 48) return 5;
	if (data[1][1] != 54) return 6;
																																																			}
	if (value != 50) return 4;
	if (record.item != 48) return 5;
	if (data[1][1] != 51) return 6;
																																																		}
	if (value != 49) return 4;
	if (record.item != 48) return 5;
	if (data[1][1] != 51) return 6;
																																																	}
	if (value != 48) return 4;
	if (record.item != 48) return 5;
	if (data[1][1] != 51) return 6;
																																																}
	if (value != 47) return 4;
	if (record.item != 44) return 5;
	if (data[1][1] != 48) return 6;
																																															}
	if (value != 46) return 4;
	if (record.item != 44) return 5;
	if (data[1][1] != 48) return 6;
																																														}
	if (value != 45) return 4;
	if (record.item != 44) return 5;
	if (data[1][1] != 48) return 6;
																																													}
	if (value != 44) return 4;
	if (record.item != 44) return 5;
	if (data[1][1] != 45) return 6;
																																												}
	if (value != 43) return 4;
	if (record.item != 40) return 5;
	if (data[1][1] != 45) return 6;
																																											}
	if (value != 42) return 4;
	if (record.item != 40) return 5;
	if (data[1][1] != 45) return 6;
																																										}
	if (value != 41) return 4;
	if (record.item != 40) return 5;
	if (data[1][1] != 42) return 6;
																																									}
	if (value != 40) return 4;
	if (record.item != 40) return 5;
	if (data[1][1] != 42) return 6;
																																								}
	if (value != 39) return 4;
	if (record.item != 36) return 5;
	if (data[1][1] != 42) return 6;
																																							}
	if (value != 38) return 4;
	if (record.item != 36) return 5;
	if (data[1][1] != 39) return 6;
																																						}
	if (value != 37) return 4;
	if (record.item != 36) return 5;
	if (data[1][1] != 39) return 6;
																																					}
	if (value != 36) return 4;
	if (record.item != 36) return 5;
	if (data[1][1] != 39) return 6;
																																				}
	if (value != 35) return 4;
	if (record.item != 32) return 5;
	if (data[1][1] != 36) return 6;
																																			}
	if (value != 34) return 4;
	if (record.item != 32) return 5;
	if (data[1][1] != 36) return 6;
																																		}
	if (value != 33) return 4;
	if (record.item != 32) return 5;
	if (data[1][1] != 36) return 6;
																																	}
	if (value != 32) return 4;
	if (record.item != 32) return 5;
	if (data[1][1] != 33) return 6;
																																}
	if (value != 31) return 4;
	if (record.item != 28) return 5;
	if (data[1][1] != 33) return 6;
																															}
	if (value != 30) return 4;
	if (record.item != 28) return 5;
	if (data[1][1] != 33) return 6;
																														}
	if (value != 29) return 4;
	if (record.item != 28) return 5;
	if (data[1][1] != 30) return 6;
																													}
	if (value != 28) return 4;
	if (record.item != 28) return 5;
	if (data[1][1] != 30) return 6;
																												}
	if (value != 27) return 4;
	if (record.item != 24) return 5;
	if (data[1][1] != 30) return 6;
																											}
	if (value != 26) return 4;
	if (record.item != 24) return 5;
	if (data[1][1] != 27) return 6;
																										}
	if (value != 25) return 4;
	if (record.item != 24) return 5;
	if (data[1][1] != 27) return 6;
																									}
	if (value != 24) return 4;
	if (record.item != 24) return 5;
	if (data[1][1] != 27) return 6;
																								}
	if (value != 23) return 4;
	if (record.item != 20) return 5;
	if (data[1][1] != 24) return 6;
																							}
	if (value != 22) return 4;
	if (record.item != 20) return 5;
	if (data[1][1] != 24) return 6;
																						}
	if (value != 21) return 4;
	if (record.item != 20) return 5;
	if (data[1][1] != 24) return 6;
																					}
	if (value != 20) return 4;
	if (record.item != 20) return 5;
	if (data[1][1] != 21) return 6;
																				}
	if (value != 19) return 4;
	if (record.item != 16) return 5;
	if (data[1][1] != 21) return 6;
																			}
	if (value != 18) return 4;
	if (record.item != 16) return 5;
	if (data[1][1] != 21) return 6;
																		}
	if (value != 17) return 4;
	if (record.item != 16) return 5;
	if (data[1][1] != 18) return 6;
																	}
	if (value != 16) return 4;
	if (record.item != 16) return 5;
	if (data[1][1] != 18) return 6;
																}
	if (value != 15) return 4;
	if (record.item != 12) return 5;
	if (data[1][1] != 18) return 6;
															}
	if (value != 14) return 4;
	if (record.item != 12) return 5;
	if (data[1][1] != 15) return 6;
														}
	if (value != 13) return 4;
	if (record.item != 12) return 5;
	if (data[1][1] != 15) return 6;
													}
	if (value != 12) return 4;
	if (record.item != 12) return 5;
	if (data[1][1] != 15) return 6;
												}
	if (value != 11) return 4;
	if (record.item != 8) return 5;
	if (data[1][1] != 12) return 6;
											}
	if (value != 10) return 4;
	if (record.item != 8) return 5;
	if (data[1][1] != 12) return 6;
										}
	if (value != 9) return 4;
	if (record.item != 8) return 5;
	if (data[1][1] != 12) return 6;
									}
	if (value != 8) return 4;
	if (record.item != 8) return 5;
	if (data[1][1] != 9) return 6;
								}
	if (value != 7) return 4;
	if (record.item != 4) return 5;
	if (data[1][1] != 9) return 6;
							}
	if (value != 6) return 4;
	if (record.item != 4) return 5;
	if (data[1][1] != 9) return 6;
						}
	if (value != 5) return 4;
	if (record.item != 4) return 5;
	if (data[1][1] != 6) return 6;
					}
	if (value != 4) return 4;
	if (record.item != 4) return 5;
	if (data[1][1] != 6) return 6;
				}
	if (value != 3) return 4;
	if (record.item != 0) return 5;
	if (data[1][1] != 6) return 6;
			}
	if (value != 2) return 4;
	if (record.item != 0) return 5;
	if (data[1][1] != 3) return 6;
		}
	if (value != 1) return 4;
	if (record.item != 0) return 5;
	if (data[1][1] != 3) return 6;
	}
	if (value != 0) return 4;
	if (record.item != 0) return 5;
	if (data[1][1] != 3) return 6;
	return 0;
}
