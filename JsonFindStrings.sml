structure JsonFindStrings:
sig
  val in_string_flags: char Seq.t -> bool Seq.t
end =
struct
  fun is_backslash c = (c = #"\\")
  fun is_quote c = (c = #"\"")

  (* In order to parse JSON, we have to be careful about what is and isn't
   * inside of a JSON string.
   *
   * Here, you will implement a function `in_string_flags(json_chars)`
   * which returns a sequence of booleans, indicating for each character of
   * the input whether or not that character is part of a JSON string. You
   * should include the indices of the start and end quotes.
   *
   * For example:
   *   input       {"hello":[{"world":1},[1,["2",3],4]]}
   *   output      0111111100011111110000000011100000000   (where 1 means true
   *                                                        and 0 means false)
   *
   * Every JSON string begins and ends with a double quote character: \"
   * Inside of a JSON string, there can be escape sequences. Valid JSON
   * escape sequences are:
   *
   *   \"  \\  \/  \b  \f  \n  \r  \t  \uXXXX   (where X is a hex digit)
   *
   * The tricky ones to handle in your solution are going to be the escape
   * sequences \" and \\. Any escaped quote character does _not_ terminate
   * the string.
   *
   * It might be tempting to attempt to solve the problem simply by checking if
   * the character immediately preceding a quote is a backslash. However, it's
   * not quite so simple, because the backslash might itself have been escaped!
   * For example, all of these are valid JSON strings:
   *
   *   "\""
   *   "\\"
   *   "\\\""
   *   "\\\\\\\"\\\\\\"
   *
   * Notice that, in any contiguous run of backslashes, it is important
   * whether or not the length is odd or even.
   *
   * An important property of JSON that you can take advantage of here is
   * that, if you see a backslash, you _must_ be inside of a string. This just
   * happens to be true of the JSON grammar; backslashes aren't permitted
   * elsewhere. More info about the JSON grammar can be found here:
   *   https://www.json.org/json-en.html
   *
   * We encourage using functions such as the following:
   *
   *   Parallel.tabulate: (int * int) -> (int -> 'a) -> 'a Seq.t
   *   Parallel.reduce: ('a * 'a -> 'a) -> 'a -> (int * int) -> (int -> 'a) -> 'a
   *   Parallel.scan: ('a * 'a -> 'a) -> 'a -> (int * int) -> (int -> 'a) -> 'a Seq.t
   *                  (* length N+1 *)
   *   Parallel.filter: (int * int) -> (int -> 'a) -> (int -> bool) -> 'a Seq.t
   *
   * There are also a couple utility function defined above; feel free to add
   * more or edit these as desired.
   *
   *   is_quote: char -> bool
   *   is_backslash: char -> bool
   *
   * As always, you will need functions on sequences:
   *
   *   Seq.length: 'a Seq.t -> int           O(1) work, O(1) span
   *   Seq.nth: 'a Seq.t -> int -> 'a        O(1) work, O(1) span
   *
   * You may assume that tabulate, reduce, scan, and filter all have linear
   * work and polylogarithmic span, assuming the functions given as argument
   * cost O(1) work and span.
   *
   * Formally, assuming the functions f, g, and p all require O(1) work and
   * span, you may assume the following cost specifications:
   *   tabulate (lo, hi) f        O(hi-lo) work, O(log(hi-lo)) span
   *   reduce g z (lo, hi) f      O(hi-lo) work, O(log(hi-lo)) span
   *   scan g z (lo, hi) f        O(hi-lo) work, O(log(hi-lo)) span
   *   filter (lo, hi) f p        O(hi-lo) work, O(log(hi-lo)) span
   *
   * COST REQUIREMENT:
   *   **Your solution must have O(N) work and O(polylog(N)) span**,
   *   where N is the length (number of characters) of the input.
   *
   * HINT: For any contiguous run of backslashes, consider computing the
   * index of where that run of backslashes began.
   *
   *         chars: \" \ \ \ " _ \ \ _ "
   *         index: 0 1 2 3 4 5 6 7 8 9
   *    run_starts: . 1 1 1 . . 6 6 . .
   *)

  fun in_string_flags json_chars =
    let
      fun f i =
        if is_backslash (Seq.nth json_chars i) andalso
           (is_backslash (Seq.nth json_chars (i - 1)) = false)
        then
          i
        else if is_backslash (Seq.nth json_chars i) then
          ~1
        else
          0

      (* 1-> n *)
      val backslash =
        Parallel.scan (fn (a, b) => Int.max (b, a)) 0 (0, Seq.length json_chars) f

      fun prefix_f i =
        if is_quote (Seq.nth json_chars i) andalso
           (i > 0 andalso is_backslash (Seq.nth json_chars (i - 1)) andalso
            (((i - 1) - Seq.nth backslash i + 1) mod 2) = 1)
        then
          0
        else if is_quote (Seq.nth json_chars i) then
          1
        else
          0

      val prefixQuotes =
        Parallel.scan (fn (a, b) => a + b) 0 (0, Seq.length json_chars - 1) prefix_f

      fun f i =
        let
          val isodd = (Seq.nth prefixQuotes i) mod 2 = 1
          val currentIsQuote = is_quote (Seq.nth json_chars i)
        in
          isodd orelse currentIsQuote
        end

      val ans = Parallel.tabulate (0, Seq.length prefixQuotes) f
    in
      ans
    end
end
