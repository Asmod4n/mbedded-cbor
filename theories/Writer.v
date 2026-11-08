(* The writer of tag 28 (shareable) and tag 29 (sharedref) of the value-sharing registration,
   http://cbor.schmorp.de/value-sharing, and its round trip through the reader of ValueSharing.v.
   The writer marks a node only when a cycle reaches it. A node that two paths reach without a cycle is
   written in full each time. *)

From Stdlib Require Import List Arith Lia PeanoNat.
From MbeddedCbor Require Import ValueSharing.
From MbeddedCbor Require Meaning.
Import ListNotations.

(* The value of the first entry with the key k. A graph is read with it. *)
Fixpoint assoc {A : Type} (l : list (nat * A)) (k : nat) : option A :=
  match l with
  | [] => None
  | (k', a) :: l' => if Nat.eqb k' k then Some a else assoc l' k
  end.

Definition mem (v : nat) (l : list nat) : bool := existsb (Nat.eqb v) l.

Lemma mem_In : forall v l, mem v l = true <-> In v l.
Proof.
  intros v l. unfold mem. rewrite existsb_exists. split.
  - intros [x [H E]]. apply Nat.eqb_eq in E. subst. exact H.
  - intros H. exists v. split; [exact H | apply Nat.eqb_refl].
Qed.

Lemma mem_false : forall v l, mem v l = false <-> ~ In v l.
Proof.
  intros v l. rewrite <- mem_In.
  destruct (mem v l); split; intros H; [discriminate | exfalso; apply H; reflexivity | intros E; discriminate | reflexivity].
Qed.

Lemma assoc_In : forall A (l : list (nat * A)) k a, assoc l k = Some a -> In (k, a) l.
Proof.
  intros A. induction l as [| [k' a'] l IH]; intros k a H; simpl in H; [discriminate |].
  destruct (Nat.eqb k' k) eqn:E.
  - apply Nat.eqb_eq in E. inversion H; subst. left. reflexivity.
  - right. apply IH. exact H.
Qed.

Lemma assoc_key : forall A (l : list (nat * A)) k a, assoc l k = Some a -> In k (map fst l).
Proof. intros A l k a H. apply assoc_In in H. apply (in_map fst) in H. exact H. Qed.

Lemma assoc_None : forall A (l : list (nat * A)) k, assoc l k = None <-> ~ In k (map fst l).
Proof.
  intros A. induction l as [| [k' a'] l IH]; intros k; simpl.
  - split; [intros _ [] | reflexivity].
  - destruct (Nat.eqb k' k) eqn:E.
    + apply Nat.eqb_eq in E. subst. split; [discriminate | intros H; exfalso; apply H; left; reflexivity].
    + apply Nat.eqb_neq in E. rewrite IH. split.
      * intros H [H1 | H1]; [exact (E H1) | exact (H H1)].
      * intros H H1. apply H. right. exact H1.
Qed.

Lemma assoc_NoDup : forall A (l : list (nat * A)) k a, NoDup (map fst l) -> In (k, a) l -> assoc l k = Some a.
Proof.
  intros A. induction l as [| [k' a'] l IH]; intros k a N H; simpl; [destruct H |].
  simpl in N. inversion N as [| ? ? Nk Nl]; subst.
  destruct H as [H | H].
  - inversion H; subst. rewrite Nat.eqb_refl. reflexivity.
  - destruct (Nat.eqb k' k) eqn:E.
    + apply Nat.eqb_eq in E. subst. exfalso. apply Nk. apply (in_map fst) in H. exact H.
    + apply IH; assumption.
Qed.

Lemma assoc_mem : forall A (l : list (nat * A)) v, mem v (map fst l) = match assoc l v with Some _ => true | None => false end.
Proof.
  intros A l v. destruct (assoc l v) as [a |] eqn:E.
  - apply mem_In. eapply assoc_key. exact E.
  - apply mem_false. apply assoc_None. exact E.
Qed.

(* assoc on a graph gives what Meaning.find gives. Meaning.unfold reads a graph with Meaning.find. *)
Lemma assoc_find : forall (g : list (nat * node)) i, assoc g i = Meaning.find g i.
Proof. induction g as [| [j n] g IH]; intros i; simpl; [reflexivity | rewrite IH; reflexivity]. Qed.

(* Every child of a node of g is a node of g. *)
Definition closed (g : list (nat * node)) : Prop :=
  forall i n, assoc g i = Some n -> Forall (fun c => In c (map fst g)) (children n).

(* No node of g is a tag 28 or a tag 29. The reader takes these tags as marks and references. *)
Definition plain_tags (g : list (nat * node)) : Prop :=
  forall i t c, assoc g i = Some (NTag t c) -> t <> shareable /\ t <> sharedref.

(* The head of a node (RFC 8949 3): its major type and its argument. *)
Definition item_head (n : node) : tok :=
  match n with
  | NUInt x => TUInt x
  | NLeaf x => TLeaf x
  | NArr l => TArr (length l)
  | NMap ps => TMap (length ps)
  | NTag t _ => TTag t
  end.

(* The depth-first walk from a node. The path holds the nodes that are open. The children of a node are
   visited in the order of [children]: array elements in order, the key and then the value of each map
   pair, the content of a tag. A node on the path that is reached again is the target of a back edge. A
   node that is not on the path is entered again, also when an earlier branch entered it. *)

(* back_targets lists the target of each back edge of the walk, in the order of the walk. *)
Fixpoint back_targets (g : list (nat * node)) (fuel : nat) (path : list nat) (v : nat) : list nat :=
  if mem v path then [v] else
  match fuel with
  | 0 => []
  | S f =>
    match assoc g v with
    | None => []
    | Some n => flat_map (back_targets g f (v :: path)) (children n)
    end
  end.

(* preorder lists each node that the walk enters, in the order of the walk. *)
Fixpoint preorder (g : list (nat * node)) (fuel : nat) (path : list nat) (v : nat) : list nat :=
  if mem v path then [] else
  match fuel with
  | 0 => []
  | S f =>
    match assoc g v with
    | None => []
    | Some n => v :: flat_map (preorder g f (v :: path)) (children n)
    end
  end.

(* The nodes that a back edge of the walk from root reaches, each one once. The fuel is the size of the
   graph: the path holds distinct nodes of the graph (see back_targets_fuel). *)
Definition back_edges (g : list (nat * node)) (root : nat) : list nat :=
  nodup Nat.eq_dec (back_targets g (length g) [] root).

(* write_items writes the items of a list one after the other and passes the count of marks along. The step
   takes the count of marks and the item. *)
Fixpoint write_items (step : nat -> nat -> list tok * nat) (l : list nat) (m : nat) : list tok * nat :=
  match l with
  | [] => ([], m)
  | c :: l' =>
    let '(ts1, m1) := step m c in
    let '(ts2, m2) := write_items step l' m1 in
    (ts1 ++ ts2, m2)
  end.

(* write_item writes the node v in preorder and gives the stream and the count of marks written so far.
   The path holds each open node with the index of its mark. A node on the path is written as 29(index).
   A node that is not on the path is written in full: a node of be gets a tag 28 before its head, and its
   index is the count m of the marks before it. *)
Fixpoint write_item (g : list (nat * node)) (be : list nat) (fuel : nat) (path : list (nat * nat)) (m : nat)
  (v : nat) {struct fuel} : list tok * nat :=
  match assoc path v with
  | Some i => ([TTag sharedref; TUInt i], m)
  | None =>
    match fuel with
    | 0 => ([], m)
    | S f =>
      match assoc g v with
      | None => ([], m)
      | Some n =>
        if mem v be then
          let '(body, m2) := write_items (write_item g be f ((v, m) :: path)) (children n) (S m) in
          (TTag shareable :: item_head n :: body, m2)
        else
          let '(body, m2) := write_items (write_item g be f ((v, m) :: path)) (children n) m in
          (item_head n :: body, m2)
      end
    end
  end.

Definition write (g : list (nat * node)) (root : nat) : list tok :=
  fst (write_item g (back_edges g root) (length g) [] 0 root).

(* The fuel is enough: every fuel at least the size of the graph gives the same result. The path holds
   distinct nodes of the graph, so it never holds more nodes than the graph. *)
Lemma path_bound : forall (g : list (nat * node)) (path : list nat) v,
  NoDup path -> incl path (map fst g) -> ~ In v path -> In v (map fst g) -> S (length path) <= length g.
Proof.
  intros g path v N I nv iv.
  assert (L : length (v :: path) <= length (map fst g)).
  { apply NoDup_incl_length; [constructor; assumption |].
    intros x [<- | H]; [exact iv | apply I; exact H]. }
  rewrite length_map in L. simpl in L. exact L.
Qed.

Lemma write_items_ext : forall st1 st2 l m,
  (forall m c, st1 m c = st2 m c) -> write_items st1 l m = write_items st2 l m.
Proof.
  intros st1 st2. induction l as [| c l IH]; intros m H; simpl; [reflexivity |].
  rewrite (H m c). destruct (st2 m c) as [ts1 m1].
  rewrite IH; [reflexivity | exact H].
Qed.

Lemma incl_cons_path : forall (g : list (nat * node)) path v,
  incl path (map fst g) -> In v (map fst g) -> incl (v :: path) (map fst g).
Proof. intros g path v I H x [<- | Hx]; [exact H | apply I; exact Hx]. Qed.

Theorem write_item_fuel : forall g be f1 f2 path m v,
  NoDup (map fst path) -> incl (map fst path) (map fst g) ->
  length g <= f1 + length path -> length g <= f2 + length path ->
  write_item g be f1 path m v = write_item g be f2 path m v.
Proof.
  intros g be. induction f1 as [| f1 IH]; intros f2 path m v N I L1 L2;
    destruct f2 as [| f2]; simpl; destruct (assoc path v) as [i |] eqn:P; try reflexivity;
    (destruct (assoc g v) as [n |] eqn:G; [| reflexivity]);
    (assert (B := path_bound g (map fst path) v N I ltac:(apply assoc_None; exact P) ltac:(eapply assoc_key; exact G)));
    rewrite length_map in B; try lia.
  destruct (mem v be);
    (rewrite (write_items_ext _ (write_item g be f2 ((v, m) :: path)));
     [reflexivity | intros m' c; apply IH; simpl;
       [constructor; [apply assoc_None; exact P | exact N] | apply incl_cons_path; [exact I | eapply assoc_key; exact G] | lia | lia]]).
Qed.

Theorem back_targets_fuel : forall g f1 f2 path v,
  NoDup path -> incl path (map fst g) -> length g <= f1 + length path -> length g <= f2 + length path ->
  back_targets g f1 path v = back_targets g f2 path v.
Proof.
  intros g. induction f1 as [| f1 IH]; intros f2 path v N I L1 L2;
    destruct f2 as [| f2]; simpl; destruct (mem v path) eqn:P; try reflexivity;
    (destruct (assoc g v) as [n |] eqn:G; [| reflexivity]);
    (assert (B := path_bound g path v N I ltac:(apply mem_false; exact P) ltac:(eapply assoc_key; exact G)));
    try lia.
  apply flat_map_ext. intros c. apply IH; simpl;
    [constructor; [apply mem_false; exact P | exact N] | apply incl_cons_path; [exact I | eapply assoc_key; exact G] | lia | lia].
Qed.

Theorem preorder_fuel : forall g f1 f2 path v,
  NoDup path -> incl path (map fst g) -> length g <= f1 + length path -> length g <= f2 + length path ->
  preorder g f1 path v = preorder g f2 path v.
Proof.
  intros g. induction f1 as [| f1 IH]; intros f2 path v N I L1 L2;
    destruct f2 as [| f2]; simpl; destruct (mem v path) eqn:P; try reflexivity;
    (destruct (assoc g v) as [n |] eqn:G; [| reflexivity]);
    (assert (B := path_bound g path v N I ltac:(apply mem_false; exact P) ltac:(eapply assoc_key; exact G)));
    try lia.
  f_equal. apply flat_map_ext. intros c. apply IH; simpl;
    [constructor; [apply mem_false; exact P | exact N] | apply incl_cons_path; [exact I | eapply assoc_key; exact G] | lia | lia].
Qed.

(* The count of the heads TTag t in a stream. *)
Definition count_tag (t : nat) (ts : list tok) : nat :=
  length (filter (fun x => match x with TTag t' => Nat.eqb t' t | _ => false end) ts).

Lemma list_sum_lin : forall (l : list nat) (X Y Z : nat -> nat) a b c,
  list_sum (map (fun x => a * X x + b * Y x + c * Z x) l)
  = a * list_sum (map X l) + b * list_sum (map Y l) + c * list_sum (map Z l).
Proof. induction l as [| x l IH]; intros; simpl; [lia | rewrite IH; ring]. Qed.

Lemma length_flat_map_sum : forall (f : nat -> list nat) l,
  length (flat_map f l) = list_sum (map (fun x => length (f x)) l).
Proof. induction l as [| x l IH]; simpl; [reflexivity | rewrite length_app, IH; reflexivity]. Qed.

Lemma filter_flat_map : forall (p : nat -> bool) (f : nat -> list nat) l,
  filter p (flat_map f l) = flat_map (fun x => filter p (f x)) l.
Proof. induction l as [| x l IH]; simpl; [reflexivity | rewrite filter_app, IH; reflexivity]. Qed.

Lemma write_items_sum : forall (cnt : list tok -> nat),
  cnt [] = 0 -> (forall a b, cnt (a ++ b) = cnt a + cnt b) ->
  forall st (h : nat -> nat) l m, (forall c m, In c l -> cnt (fst (st m c)) = h c) ->
  cnt (fst (write_items st l m)) = list_sum (map h l).
Proof.
  intros cnt C0 Capp st h. induction l as [| c l IH]; intros m H; simpl; [exact C0 |].
  destruct (st m c) as [ts1 m1] eqn:E1. destruct (write_items st l m1) as [ts2 m2] eqn:E2. simpl.
  rewrite Capp. f_equal.
  - specialize (H c m (or_introl eq_refl)). rewrite E1 in H. exact H.
  - specialize (IH m1 (fun c' m' H' => H c' m' (or_intror H'))). rewrite E2 in IH. exact IH.
Qed.

(* A measure of a stream that adds over concatenation, written as the counts of the walk: a for each back
   reference, b for each mark, c for each head of a node written in full. *)
Lemma write_item_cost : forall g be (cnt : list tok -> nat) (a b c : nat),
  cnt [] = 0 -> (forall x y, cnt (x ++ y) = cnt x + cnt y) ->
  (forall i, cnt [TTag sharedref; TUInt i] = a) -> cnt [TTag shareable] = b ->
  (forall i n, assoc g i = Some n -> cnt [item_head n] = c) ->
  forall f path m v,
    cnt (fst (write_item g be f path m v)) =
      a * length (back_targets g f (map fst path) v)
      + b * length (filter (fun u => mem u be) (preorder g f (map fst path) v))
      + c * length (preorder g f (map fst path) v).
Proof.
  intros g be cnt a b c C0 Capp Cref Cmark Chead.
  induction f as [| f IH]; intros path m v; simpl; rewrite assoc_mem; destruct (assoc path v) as [i |] eqn:P.
  - simpl. rewrite Cref. lia.
  - simpl. rewrite C0. lia.
  - simpl. rewrite Cref. lia.
  - destruct (assoc g v) as [n |] eqn:G; [| simpl; rewrite C0; lia].
    assert (Sum : forall m1, cnt (fst (write_items (write_item g be f ((v, m) :: path)) (children n) m1))
      = a * length (flat_map (back_targets g f (v :: map fst path)) (children n))
        + b * length (filter (fun u => mem u be) (flat_map (preorder g f (v :: map fst path)) (children n)))
        + c * length (flat_map (preorder g f (v :: map fst path)) (children n))).
    { intros m1. rewrite (write_items_sum cnt C0 Capp _
        (fun x => a * length (back_targets g f (v :: map fst path) x)
          + b * length (filter (fun u => mem u be) (preorder g f (v :: map fst path) x))
          + c * length (preorder g f (v :: map fst path) x))).
      - rewrite list_sum_lin, filter_flat_map, !length_flat_map_sum. reflexivity.
      - intros x m2 _. rewrite IH. reflexivity. }
    destruct (mem v be) eqn:M.
    + destruct (write_items (write_item g be f ((v, m) :: path)) (children n) (S m)) as [body m2] eqn:W.
      specialize (Sum (S m)). rewrite W in Sum. simpl in Sum |- *. rewrite M.
      change (TTag shareable :: item_head n :: body) with ([TTag shareable] ++ [item_head n] ++ body).
      rewrite !Capp, Cmark, (Chead v n G), Sum. simpl. lia.
    + destruct (write_items (write_item g be f ((v, m) :: path)) (children n) m) as [body m2] eqn:W.
      specialize (Sum m). rewrite W in Sum. simpl in Sum |- *. rewrite M.
      change (item_head n :: body) with ([item_head n] ++ body).
      rewrite !Capp, (Chead v n G), Sum. simpl. lia.
Qed.

Lemma back_targets_preorder : forall g f path v u,
  In u (back_targets g f path v) -> In u path \/ In u (preorder g f path v).
Proof.
  intros g. induction f as [| f IH]; intros path v u H; simpl in H |- *; destruct (mem v path) eqn:P.
  - destruct H as [<- | []]. left. apply mem_In. exact P.
  - destruct H.
  - destruct H as [<- | []]. left. apply mem_In. exact P.
  - destruct (assoc g v) as [n |]; [| destruct H].
    apply in_flat_map in H. destruct H as [c [Hc Hu]].
    destruct (IH _ _ _ Hu) as [[<- | Hp] | He].
    + right. left. reflexivity.
    + left. exact Hp.
    + right. right. apply in_flat_map. exists c. split; assumption.
Qed.

(* T8 (where the marks are). The stream has one tag 28 for each time that the walk enters a node of
   back_edges, and one tag 29 for each back edge. Each node of back_edges is entered, so it gets a mark.
   The writer marks a node of back_edges at each full write of it, also where no tag 29 follows that mark.
   An example is R -> [A, X], A -> [X], X -> [A] (write_two_cycles): the first full writes of A and X both get
   a mark, and only the mark of A gets a reference. The specification allows a mark with no reference. *)
Theorem marks_on_back_edges : forall g root,
  plain_tags g ->
  count_tag shareable (write g root)
    = length (filter (fun v => mem v (back_edges g root)) (preorder g (length g) [] root))
  /\ count_tag sharedref (write g root) = length (back_targets g (length g) [] root)
  /\ incl (back_edges g root) (preorder g (length g) [] root).
Proof.
  intros g root Hp. unfold write. split; [| split].
  - rewrite (write_item_cost g (back_edges g root) (count_tag shareable) 0 1 0); [simpl; lia | reflexivity | | | reflexivity |].
    + intros x y. unfold count_tag. rewrite filter_app, length_app. reflexivity.
    + intros i. reflexivity.
    + intros i n G. destruct n as [x | x | l | ps | t c]; try reflexivity.
      unfold count_tag. simpl. destruct (Hp i t c G) as [H1 _]. apply Nat.eqb_neq in H1. rewrite H1. reflexivity.
  - rewrite (write_item_cost g (back_edges g root) (count_tag sharedref) 1 0 0); [simpl; lia | reflexivity | | | reflexivity |].
    + intros x y. unfold count_tag. rewrite filter_app, length_app. reflexivity.
    + intros i. reflexivity.
    + intros i n G. destruct n as [x | x | l | ps | t c]; try reflexivity.
      unfold count_tag. simpl. destruct (Hp i t c G) as [_ H2]. apply Nat.eqb_neq in H2. rewrite H2. reflexivity.
  - intros u H. unfold back_edges in H. apply nodup_In in H.
    destruct (back_targets_preorder _ _ _ _ _ H) as [[] | He]. exact He.
Qed.

(* T10 (size). The size of the unfolding of g at root where only the back edges stay references, counted in
   heads: one head for each node written in full, one tag 28 for each node of back_edges written in full,
   and a tag 29 with its index for each back edge. *)
Definition unfolded_size (g : list (nat * node)) (root : nat) : nat :=
  length (preorder g (length g) [] root)
  + length (filter (fun v => mem v (back_edges g root)) (preorder g (length g) [] root))
  + 2 * length (back_targets g (length g) [] root).

Theorem write_size : forall g root, length (write g root) = unfolded_size g root.
Proof.
  intros g root. unfold write, unfolded_size.
  rewrite (write_item_cost g (back_edges g root) (@length tok) 2 1 1); [simpl; lia | reflexivity | | | reflexivity |].
  - intros x y. apply length_app.
  - intros i. reflexivity.
  - intros i n _. reflexivity.
Qed.

(* Two nodes agree in their kind and their data, and P pairs their children in order. *)
Definition same_shape (P : list (nat * nat)) (a b : node) : Prop :=
  match a, b with
  | NUInt x, NUInt y => x = y
  | NLeaf x, NLeaf y => x = y
  | NArr l1, NArr l2 => Forall2 (fun i j => In (i, j) P) l1 l2
  | NMap l1, NMap l2 => Forall2 (fun p q => In (fst p, fst q) P /\ In (snd p, snd q) P) l1 l2
  | NTag t1 c1, NTag t2 c2 => t1 = t2 /\ In (c1, c2) P
  | _, _ => False
  end.

Lemma same_shape_mono : forall P P' a b, incl P P' -> same_shape P a b -> same_shape P' a b.
Proof.
  intros P P' a b I C. destruct a, b; simpl in C |- *; try contradiction; try assumption.
  - eapply Forall2_impl; [| exact C]. intros; apply I; assumption.
  - eapply Forall2_impl; [| exact C]. intros x y [H1 H2]; split; apply I; assumption.
  - destruct C as [E H]; split; [exact E | apply I; exact H].
Qed.

Lemma Forall2_map_eq : forall A B C (R : A -> B -> Prop) (f : A -> C) (h : B -> C) l1 l2,
  (forall a b, R a b -> f a = h b) -> Forall2 R l1 l2 -> map f l1 = map h l2.
Proof. intros A B C R f h l1 l2 H F. induction F; simpl; [reflexivity | f_equal; auto]. Qed.

(* A relation P where every pair has the same shape is a bisimulation: its pairs unfold alike at each depth. *)
Lemma unfold_same_shape : forall G g P,
  (forall a b, In (a, b) P -> exists na nb, assoc G a = Some na /\ assoc g b = Some nb /\ same_shape P na nb) ->
  forall k a b, In (a, b) P -> Meaning.unfold G k a = Meaning.unfold g k b.
Proof.
  intros G g P H. induction k as [| k IH]; intros a b I; [reflexivity |].
  destruct (H a b I) as [na [nb [Ea [Eb C]]]]. simpl. rewrite <- !assoc_find, Ea, Eb.
  destruct na, nb; simpl in C; try contradiction.
  - subst; reflexivity.
  - subst; reflexivity.
  - f_equal. eapply Forall2_map_eq; [| exact C]. intros i j Hij. apply IH. exact Hij.
  - f_equal. eapply Forall2_map_eq; [| exact C]. intros p q [H1 H2]. f_equal; apply IH; assumption.
  - destruct C as [-> Ic]. f_equal. apply IH. exact Ic.
Qed.

(* The node that the reader makes from a head and the ids of its children. *)
Definition rebuild (n : node) (ids : list nat) : node :=
  match n with
  | NArr _ => NArr ids
  | NMap _ => NMap (pairs ids)
  | NTag t _ => NTag t (hd 0 ids)
  | _ => n
  end.

Lemma children_length_map : forall ps : list (nat * nat), length (children (NMap ps)) = length ps + length ps.
Proof. induction ps as [| p ps IH]; simpl; [reflexivity |]. simpl in IH. rewrite IH. lia. Qed.

Lemma pairs_Forall2 : forall (R : nat -> nat -> Prop) ids (ps : list (nat * nat)),
  Forall2 R ids (flat_map (fun p => [fst p; snd p]) ps) ->
  Forall2 (fun p q => R (fst p) (fst q) /\ R (snd p) (snd q)) (pairs ids) ps.
Proof.
  intros R ids ps. revert ids. induction ps as [| [a b] ps IH]; intros ids F; simpl in F.
  - inversion F; subst. constructor.
  - inversion F as [| x1 y1 l1 l1' R1 F1]; subst. inversion F1 as [| x2 y2 l2 l2' R2 F2]; subst.
    simpl. constructor; [split; assumption | apply IH; exact F2].
Qed.

Lemma same_shape_rebuild : forall P n ids,
  Forall2 (fun a b => In (a, b) P) ids (children n) -> same_shape P (rebuild n ids) n.
Proof.
  intros P n ids F. destruct n as [x | x | l | ps | t c]; simpl in F |- *.
  - reflexivity.
  - reflexivity.
  - exact F.
  - exact (pairs_Forall2 (fun a b => In (a, b) P) ids ps F).
  - inversion F as [| a b l l' I F']; subst. inversion F'; subst. simpl. split; [reflexivity | exact I].
Qed.

(* The reader on the head of a node and the items of its children. *)
Lemma read_node : forall n F body rest s0 ids s2,
  (forall t c, n = NTag t c -> t <> shareable /\ t <> sharedref) ->
  items (read_item F) (length (children n)) (body ++ rest) (reserved (looked 1 s0)) = ok (ids, rest, s2) ->
  read_item (S F) (item_head n :: body ++ rest) s0 = ok (next s0, rest, made (next s0) (rebuild n ids) s2).
Proof.
  intros n F body rest s0 ids s2 Hn E. destruct n as [x | x | l | ps | t c].
  - simpl in E |- *. injection E as E1 E2 E3. subst. rewrite E2. reflexivity.
  - simpl in E |- *. injection E as E1 E2 E3. subst. rewrite E2. reflexivity.
  - simpl in E |- *. rewrite E. reflexivity.
  - rewrite children_length_map in E. simpl in E |- *. rewrite E. reflexivity.
  - destruct (Hn t c eq_refl) as [H1 H2]. apply Nat.eqb_neq in H1, H2.
    simpl in E |- *. rewrite H1, H2.
    destruct (read_item F (body ++ rest) (reserved (looked 1 s0))) as [[[c' r1] s1] | e] eqn:Er; [| discriminate].
    simpl in E. inversion E; subst. reflexivity.
Qed.

(* The reader on a tag 28 before the head of a node: the mark names the node, and the table gets its entry. *)
Lemma read_mark : forall n F x s,
  (forall t c, n = NTag t c -> t <> shareable /\ t <> sharedref) ->
  read_item (S F) (TTag shareable :: item_head n :: x) s
  = read_item F (item_head n :: x) (marked_as (next s) 1 (looked 1 s)).
Proof.
  intros n F x s Hn. destruct n as [y | y | l | ps | t c]; try reflexivity.
  destruct (Hn t c eq_refl) as [H1 H2]. apply Nat.eqb_neq in H1, H2.
  simpl. rewrite H1. simpl. rewrite H2. reflexivity.
Qed.

Section RoundTrip.

Variable g : list (nat * node).
Variable be : list nat.
Hypothesis g_closed : closed g.
Hypothesis g_plain : plain_tags g.

(* Each open node of be has its mark in the table of the reader, and Q pairs the id of that mark with it. *)
Definition marks_in_table (s : state) (path : list (nat * nat)) (Q : list (nat * nat)) : Prop :=
  forall u i, assoc path u = Some i -> mem u be = true ->
    exists r, nth_error (table s) i = Some r /\ r < next s /\ In (r, u) Q.

(* Each pair of N names a node of G and a node of g with the same shape under P. *)
Definition bisim_pairs (G : list (nat * node)) (P N : list (nat * nat)) : Prop :=
  forall a b, In (a, b) N -> exists na nb, In (a, na) G /\ assoc g b = Some nb /\ same_shape P na nb.

Lemma marks_in_table_extends : forall s s' path Q, extends s s' -> marks_in_table s path Q -> marks_in_table s' path Q.
Proof.
  intros s s' path Q [_ [[t T] L]] H u i P M.
  destruct (H u i P M) as [r [E [Lr I]]].
  exists r. split; [rewrite T; apply nth_error_app_prefix; exact E | split; [lia | exact I]].
Qed.

Lemma extends_graph : forall s s' e, extends s s' -> In e (graph s) -> In e (graph s').
Proof. intros s s' e [[gg G] _] H. rewrite G. apply in_or_app. right. exact H. Qed.

(* The reader reads back what write_item writes from the node v. *)
Definition reads_back (f : nat) (path : list (nat * nat)) (v : nat) : Prop :=
  forall m ts m' Q s F rest,
    write_item g be f path m v = (ts, m') ->
    marks_in_table s path Q -> length (table s) = m -> length ts < F ->
    exists r s' N,
      read_item F (ts ++ rest) s = ok (r, rest, s') /\ extends s s' /\ length (table s') = m' /\
      In (r, v) (N ++ Q) /\ (forall P, incl (N ++ Q) P -> bisim_pairs (graph s') P N).

Lemma write_items_read : forall f path l,
  (forall c, In c l -> reads_back f path c) ->
  forall m ts m' Q s F rest,
    write_items (write_item g be f path) l m = (ts, m') ->
    marks_in_table s path Q -> length (table s) = m -> length ts < F ->
    exists ids s' N,
      items (read_item F) (length l) (ts ++ rest) s = ok (ids, rest, s') /\ extends s s' /\
      length (table s') = m' /\ Forall2 (fun a b => In (a, b) (N ++ Q)) ids l /\
      (forall P, incl (N ++ Q) P -> bisim_pairs (graph s') P N).
Proof.
  intros f path. induction l as [| c l IH]; intros Hc m ts m' Q s F rest W R T L; simpl in W.
  - inversion W; subst. exists [], s, []. simpl.
    split; [reflexivity | split; [apply extends_refl | split; [reflexivity | split; [constructor | intros P _ a b []]]]].
  - destruct (write_item g be f path m c) as [ts1 m1] eqn:W1.
    destruct (write_items (write_item g be f path) l m1) as [ts2 m2] eqn:W2.
    injection W as Ets Em; subst ts m'. rewrite length_app in L.
    destruct (Hc c (or_introl eq_refl) m ts1 m1 Q s F (ts2 ++ rest) W1 R T ltac:(lia))
      as [r [s1 [N1 [E1 [X1 [T1 [I1 B1]]]]]]].
    destruct (IH (fun c' H' => Hc c' (or_intror H')) m1 ts2 m2 Q s1 F rest W2 (marks_in_table_extends _ _ _ _ X1 R) T1 ltac:(lia))
      as [ids [s2 [N2 [E2 [X2 [T2 [F2 B2]]]]]]].
    exists (r :: ids), s2, (N1 ++ N2). simpl. rewrite <- app_assoc, E1, E2.
    split; [reflexivity |]. split; [eapply extends_trans; eassumption |]. split; [exact T2 |]. split.
    + constructor.
      * rewrite !in_app_iff in *. tauto.
      * eapply Forall2_impl; [| exact F2]. intros a b Hab. rewrite !in_app_iff in *. tauto.
    + intros P IP a b Iab. apply in_app_or in Iab. destruct Iab as [Iab | Iab].
      * destruct (B1 P ltac:(intros x Hx; apply IP; rewrite !in_app_iff in *; tauto) a b Iab) as [na [nb [Ga [Gb C]]]].
        exists na, nb. split; [eapply extends_graph; eassumption | split; assumption].
      * apply B2; [| exact Iab]. intros x Hx. apply IP. rewrite !in_app_iff in *. tauto.
Qed.

Lemma reads_back_ref : forall f path v i,
  assoc path v = Some i -> incl (back_targets g f (map fst path) v) be -> reads_back f path v.
Proof.
  intros f path v i P Rv m ts m' Q s F rest W R T L.
  assert (W' : write_item g be f path m v = ([TTag sharedref; TUInt i], m)) by (destruct f; simpl; rewrite P; reflexivity).
  rewrite W' in W. injection W as Ets Em; subst ts m'.
  assert (Mv : mem v (map fst path) = true) by (apply mem_In; eapply assoc_key; exact P).
  assert (Rb : mem v be = true).
  { apply mem_In. apply Rv. destruct f; simpl; rewrite Mv; left; reflexivity. }
  destruct (R v i P Rb) as [r [E [Lr I]]].
  destruct F as [| F]; simpl in L; [lia |].
  exists r, (looked 2 s), [].
  split; [simpl; rewrite E; apply Nat.ltb_lt in Lr; rewrite Lr; reflexivity |].
  split; [apply extends_looked | split; [exact T | split; [exact I | intros P0 _ a b []]]].
Qed.

Lemma write_item_read : forall f path v,
  length g <= f + length path -> NoDup (map fst path) -> incl (map fst path) (map fst g) ->
  incl (back_targets g f (map fst path) v) be -> In v (map fst g) -> reads_back f path v.
Proof.
  induction f as [| f IH]; intros path v Lf Np Ip Rv Iv;
    (destruct (assoc path v) as [i |] eqn:Pv; [eapply reads_back_ref; eassumption |]);
    (assert (Nv : ~ In v (map fst path)) by (apply assoc_None; exact Pv));
    (assert (Bound := path_bound g (map fst path) v Np Ip Nv Iv)); rewrite length_map in Bound; [lia |].
  destruct (assoc g v) as [n |] eqn:Gv; [| exfalso; apply assoc_None in Gv; exact (Gv Iv)].
  intros m ts m' Q s F rest W R T L.
  assert (Mp : mem v (map fst path) = false) by (apply mem_false; exact Nv).
  assert (Hplain : forall t c, n = NTag t c -> t <> shareable /\ t <> sharedref) by (intros t c ->; exact (g_plain v t c Gv)).
  assert (Hchild : forall c, In c (children n) -> reads_back f ((v, m) :: path) c).
  { intros c Hc. apply IH; simpl.
    - lia.
    - constructor; assumption.
    - apply incl_cons_path; assumption.
    - intros u Hu. apply Rv. simpl. rewrite Mp, Gv. apply in_flat_map. exists c. split; assumption.
    - specialize (g_closed v n Gv). rewrite Forall_forall in g_closed. apply g_closed. exact Hc. }
  (* The node itself, from its head, at the state s0 after its mark if it has one. *)
  assert (Node : forall s0 m1 Q', next s0 = next s -> extends s s0 -> length (table s0) = m1 ->
            marks_in_table (reserved (looked 1 s0)) ((v, m) :: path) Q' -> incl Q' ((next s, v) :: Q) ->
            forall body F1, write_items (write_item g be f ((v, m) :: path)) (children n) m1 = (body, m') ->
            length body < F1 ->
            exists s' N2, read_item (S F1) (item_head n :: body ++ rest) s0 = ok (next s, rest, s') /\
              extends s s' /\ length (table s') = m' /\
              (forall P, incl ((next s, v) :: N2 ++ Q) P -> bisim_pairs (graph s') P ((next s, v) :: N2))).
  { intros s0 m1 Q' N0 X0 T0 R0 IQ body F1 Wb Lb.
    destruct (write_items_read f ((v, m) :: path) (children n) Hchild m1 body m' Q' (reserved (looked 1 s0)) F1 rest
                Wb R0 T0 Lb) as [ids [s2 [N2 [E2 [X2 [T2 [F2 B2]]]]]]].
    exists (made (next s0) (rebuild n ids) s2), N2.
    split; [rewrite (read_node n F1 body rest s0 ids s2 Hplain E2), N0; reflexivity |].
    split; [eapply extends_trans; [exact X0 | eapply extends_trans; [apply extends_rl | eapply extends_trans; [exact X2 | apply extends_made]]] |].
    split; [exact T2 |].
    intros P IP a b [Hab | Hab].
    - inversion Hab; subst a b. exists (rebuild n ids), n. split; [simpl; rewrite N0; left; reflexivity |].
      split; [exact Gv |]. apply same_shape_rebuild. eapply Forall2_impl; [| exact F2].
      intros x y Hxy. apply IP. rewrite in_app_iff in Hxy. destruct Hxy as [Hxy | Hxy].
      + right. apply in_or_app. left. exact Hxy.
      + apply IQ in Hxy. destruct Hxy as [Hxy | Hxy]; [left; exact Hxy | right; apply in_or_app; right; exact Hxy].
    - assert (IP2 : incl (N2 ++ Q') P).
      { intros x Hx. apply IP. rewrite in_app_iff in Hx. destruct Hx as [Hx | Hx].
        + right. apply in_or_app. left. exact Hx.
        + apply IQ in Hx. destruct Hx as [Hx | Hx]; [left; exact Hx | right; apply in_or_app; right; exact Hx]. }
      destruct (B2 P IP2 a b Hab) as [na [nb [Ga [Gb C]]]].
      exists na, nb. split; [right; exact Ga | split; assumption]. }
  simpl in W. rewrite Pv, Gv in W.
  destruct (mem v be) eqn:Mb.
  - destruct (write_items (write_item g be f ((v, m) :: path)) (children n) (S m)) as [body m2] eqn:Wb.
    injection W as Ets Em; subst ts m2.
    simpl in L. destruct F as [| [| F1]]; [lia | lia |].
    assert (R0 : marks_in_table (reserved (looked 1 (marked_as (next s) 1 (looked 1 s)))) ((v, m) :: path) ((next s, v) :: Q)).
    { intros u i Pu Mu. simpl in Pu. destruct (Nat.eqb v u) eqn:Evu.
      - apply Nat.eqb_eq in Evu. subst u. inversion Pu; subst i. exists (next s).
        split; [| split; [simpl; lia | left; reflexivity]].
        simpl. rewrite nth_error_app2; [| lia]. rewrite T, Nat.sub_diag. reflexivity.
      - destruct (R u i Pu Mu) as [r [E [Lr I]]]. exists r.
        split; [simpl; apply nth_error_app_prefix; exact E | split; [simpl; lia | right; exact I]]. }
    destruct (Node (marked_as (next s) 1 (looked 1 s)) (S m) ((next s, v) :: Q) eq_refl
                ltac:(eapply extends_trans; [apply extends_looked | apply extends_marked_as])
                ltac:(simpl; rewrite length_app, T; simpl; lia) R0 (incl_refl _) body F1 Wb ltac:(lia))
      as [s' [N2 [E [X [T' B]]]]].
    exists (next s), s', ((next s, v) :: N2).
    split.
    { change ((TTag shareable :: item_head n :: body) ++ rest) with (TTag shareable :: item_head n :: (body ++ rest)).
      rewrite (read_mark n (S F1) (body ++ rest) s Hplain). exact E. }
    split; [exact X | split; [exact T' | split; [left; reflexivity | exact B]]].
  - destruct (write_items (write_item g be f ((v, m) :: path)) (children n) m) as [body m2] eqn:Wb.
    injection W as Ets Em; subst ts m2.
    simpl in L. destruct F as [| F1]; [lia |].
    assert (R0 : marks_in_table (reserved (looked 1 s)) ((v, m) :: path) Q).
    { intros u i Pu Mu. simpl in Pu. destruct (Nat.eqb v u) eqn:Evu.
      - apply Nat.eqb_eq in Evu. subst u. rewrite Mb in Mu. discriminate.
      - destruct (R u i Pu Mu) as [r [E [Lr I]]]. exists r.
        split; [exact E | split; [simpl; lia | exact I]]. }
    destruct (Node s m Q eq_refl (extends_refl s) T R0 (fun x Hx => or_intror Hx) body F1 Wb ltac:(lia))
      as [s' [N2 [E [X [T' B]]]]].
    exists (next s), s', ((next s, v) :: N2).
    split; [exact E |].
    split; [exact X | split; [exact T' | split; [left; reflexivity | exact B]]].
Qed.

End RoundTrip.

(* T9 (round trip). For a closed graph without nodes of tag 28 or 29, the reader reads the stream of write and
   gives a graph that unfolds at its root like g at root, at every depth. *)
Theorem write_read_back : forall g root,
  closed g -> plain_tags g -> In root (map fst g) ->
  exists root' s, read (write g root) = ok (root', s) /\
    forall k, Meaning.unfold (graph s) k root' = Meaning.unfold g k root.
Proof.
  intros g root Hc Hp Hr. unfold write.
  destruct (write_item g (back_edges g root) (length g) [] 0 root) as [ts m'] eqn:W. simpl.
  destruct (write_item_read g (back_edges g root) Hc Hp (length g) [] root ltac:(simpl; lia) (NoDup_nil _)
              (incl_nil_l _) ltac:(intros x H; unfold back_edges; apply nodup_In; exact H) Hr
              0 ts m' [] start (S (length ts)) [] W ltac:(intros u i P; discriminate) eq_refl ltac:(lia))
    as [r [s' [N [E [X [T [I B]]]]]]].
  rewrite app_nil_r in E, I.
  assert (Rd : read ts = ok (r, s')) by (unfold read; rewrite E; reflexivity).
  exists r, s'. split; [exact Rd |].
  intros k. apply (unfold_same_shape (graph s') g N); [| exact I].
  intros a b Hab. rewrite app_nil_r in B.
  destruct (B N (incl_refl _) a b Hab) as [na [nb [Ga [Gb C]]]].
  exists na, nb. split; [apply assoc_NoDup; [eapply Meaning.read_unique; exact Rd | exact Ga] | split; assumption].
Qed.

(* No node of the graph of s is a tag 28 or a tag 29. *)
Definition plain_graph (s : state) : Prop :=
  forall i t c, In (i, NTag t c) (graph s) -> t <> shareable /\ t <> sharedref.

Lemma plain_made : forall id n s1, plain_graph s1 ->
  (forall t c, n = NTag t c -> t <> shareable /\ t <> sharedref) -> plain_graph (made id n s1).
Proof.
  intros id n s1 P1 Hn i t c [E | I].
  - injection E as E1 E2. exact (Hn t c E2).
  - exact (P1 i t c I).
Qed.

(* The reader makes a node of a tag only for a tag that is not 28 and not 29. *)
Lemma read_item_plain : forall fuel ts s id rest s',
  read_item fuel ts s = ok (id, rest, s') -> plain_graph s -> plain_graph s'.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H Pl; [discriminate |].
  simpl in H. destruct ts as [| t ts]; [discriminate |].
  assert (Hit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') ->
            plain_graph s0 -> plain_graph s0').
  { intros k ts0 s0 ids r0 s0' E.
    eapply (items_step (read_item fuel) (fun a _ b _ => plain_graph a -> plain_graph b)); [| | | exact E].
    - intros s1 ts1 P1. exact P1.
    - intros a b c x y z H1 H2 Pa. exact (H2 (H1 Pa)).
    - intros ts1 s1 id1 r1 s1' E1. exact (IH _ _ _ _ _ E1). }
  destruct t as [n | n | k | k | tg].
  - inversion H; subst. apply plain_made; [exact Pl | intros t c E0; discriminate E0].
  - inversion H; subst. apply plain_made; [exact Pl | intros t c E0; discriminate E0].
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. apply plain_made; [exact (Hit _ _ _ _ _ _ E Pl) | intros t c E0; discriminate E0].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. apply plain_made; [exact (Hit _ _ _ _ _ _ E Pl) | intros t c E0; discriminate E0].
  - destruct (Nat.eqb tg shareable) eqn:M28.
    + destruct (chain ts 1) as [c content].
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      exact (IH _ _ _ _ _ H Pl).
    + destruct (Nat.eqb tg sharedref) eqn:M29.
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate].
        destruct (_ <? _); [| discriminate].
        inversion H; subst. exact Pl.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. apply plain_made; [exact (IH _ _ _ _ _ E Pl) |].
        intros t c0 E0. injection E0 as E1 _. subst t. split; apply Nat.eqb_neq; assumption.
Qed.

(* The graph that read gives is closed, has no node of tag 28 or 29, and holds its root. So write takes it. *)
Theorem read_closed_plain_tags : forall ts root s, read ts = ok (root, s) ->
  closed (graph s) /\ plain_tags (graph s) /\ In root (map fst (graph s)).
Proof.
  intros ts root s H. destruct (read_no_dangling _ _ _ H) as [Lr [Sd C]].
  split; [| split].
  - intros i n A. apply assoc_In in A. destruct (Sd i n A) as [_ F].
    refine (Forall_impl _ _ F). intros c Lc. destruct (C c Lc) as [m Im]. apply (in_map fst) in Im. exact Im.
  - assert (Pg : plain_graph s).
    { unfold read in H.
      destruct (read_item (S (length ts)) ts start) as [[[r rest] s'] | e] eqn:E; [| discriminate].
      destruct rest; [| discriminate]. inversion H; subst.
      eapply read_item_plain; [exact E | intros i t c []]. }
    intros i t c A. apply assoc_In in A. exact (Pg i t c A).
  - destruct (C root Lr) as [m Im]. apply (in_map fst) in Im. exact Im.
Qed.

(* T9, from the stream: read, then write, then read again gives a graph that unfolds at its root like the graph
   of the first read at its root, at every depth. *)
Theorem read_write_read : forall ts root s, read ts = ok (root, s) ->
  exists root' s', read (write (graph s) root) = ok (root', s') /\
    forall k, Meaning.unfold (graph s') k root' = Meaning.unfold (graph s) k root.
Proof.
  intros ts root s H. destruct (read_closed_plain_tags _ _ _ H) as [Hc [Hp Hr]].
  exact (write_read_back (graph s) root Hc Hp Hr).
Qed.

(* Cases of write with their stream, written by hand from the rules of the writer. *)

(* The specification: 28([29(0)]), an array that holds itself. *)
Example write_cyclic_array : write [(0, NArr [0])] 0 = [TTag 28; TArr 1; TTag 29; TUInt 0].
Proof. reflexivity. Qed.

(* An array that holds one empty array twice, without a cycle: the empty array is written twice. *)
Example write_shared_twice : write [(0, NArr [1; 1]); (1, NArr [])] 0 = [TArr 2; TArr 0; TArr 0].
Proof. reflexivity. Qed.

(* A cycle through a tag that is not 28: 28(1234([29(0)])). *)
Example write_cycle_through_tag :
  write [(0, NTag 1234 1); (1, NArr [0])] 0 = [TTag 28; TTag 1234; TArr 1; TTag 29; TUInt 0].
Proof. reflexivity. Qed.

(* A map that holds itself as its key: 28({29(0): 1}). *)
Example write_map_is_its_own_key :
  write [(0, NMap [(0, 1)]); (1, NUInt 1)] 0 = [TTag 28; TMap 1; TTag 29; TUInt 0; TUInt 1].
Proof. reflexivity. Qed.

(* Two arrays that hold each other, both held by the root: [28([28([29(0)])]), 28([28([29(2)])])]. The second
   branch enters the two arrays again in the other order, and each entry gets a new mark. *)
Example write_two_cycles :
  write [(0, NArr [1; 2]); (1, NArr [2]); (2, NArr [1])] 0
  = [TArr 2; TTag 28; TArr 1; TTag 28; TArr 1; TTag 29; TUInt 0;
             TTag 28; TArr 1; TTag 28; TArr 1; TTag 29; TUInt 2].
Proof. reflexivity. Qed.

(* A chain of k diamonds. The node 3j+3 is an array of the nodes 3j+2 and 3j+1. Each of these two is an array of
   the node 3j. The node 0 is the integer 0. The root is the node 3k. *)
Fixpoint diamonds (k : nat) : list (nat * node) :=
  match k with
  | 0 => [(0, NUInt 0)]
  | S j => (3 * j + 3, NArr [3 * j + 2; 3 * j + 1]) :: (3 * j + 2, NArr [3 * j]) :: (3 * j + 1, NArr [3 * j])
           :: diamonds j
  end.

(* T10, the example: three diamonds give 2^5 - 3 = 29 heads and no mark. *)
Example diamonds_3 : length (write (diamonds 3) 9) = 29 /\ count_tag shareable (write (diamonds 3) 9) = 0.
Proof. split; reflexivity. Qed.

Lemma length_diamonds : forall k, length (diamonds k) = 3 * k + 1.
Proof. induction k as [| k IH]; simpl; [reflexivity | rewrite IH; lia]. Qed.

Lemma assoc_diamonds_low : forall d j x, x <= 3 * j -> assoc (diamonds (d + j)) x = assoc (diamonds j) x.
Proof.
  induction d as [| d IH]; intros j x L; [reflexivity |].
  rewrite Nat.add_succ_l. cbn [diamonds assoc].
  replace (Nat.eqb (3 * (d + j) + 3) x) with false by (symmetry; apply Nat.eqb_neq; lia).
  replace (Nat.eqb (3 * (d + j) + 2) x) with false by (symmetry; apply Nat.eqb_neq; lia).
  replace (Nat.eqb (3 * (d + j) + 1) x) with false by (symmetry; apply Nat.eqb_neq; lia).
  apply IH. exact L.
Qed.

Lemma assoc_diamonds : forall k j x, j <= k -> x <= 3 * j -> assoc (diamonds k) x = assoc (diamonds j) x.
Proof. intros k j x Ljk L. replace k with ((k - j) + j) by lia. apply assoc_diamonds_low. exact L. Qed.

Lemma back_targets_step : forall g f path v n,
  mem v path = false -> assoc g v = Some n ->
  back_targets g (S f) path v = flat_map (back_targets g f (v :: path)) (children n).
Proof. intros g f path v n M G. simpl. rewrite M, G. reflexivity. Qed.

Lemma preorder_step : forall g f path v n,
  mem v path = false -> assoc g v = Some n ->
  preorder g (S f) path v = v :: flat_map (preorder g f (v :: path)) (children n).
Proof. intros g f path v n M G. simpl. rewrite M, G. reflexivity. Qed.

Lemma pow2_pos : forall n, 1 <= 2 ^ n.
Proof. induction n as [| n IH]; simpl; lia. Qed.

Lemma mem_above : forall x path, (forall y, In y path -> x < y) -> mem x path = false.
Proof. intros x path H. apply mem_false. intros Hx. specialize (H x Hx). lia. Qed.

(* The walk from the node 3j of a chain of diamonds has no back edge and enters 2^(j+2) - 3 nodes. *)
Lemma diamonds_walk : forall j k f path,
  j <= k -> 3 * j + 1 <= f -> (forall y, In y path -> 3 * j < y) ->
  back_targets (diamonds k) f path (3 * j) = [] /\ length (preorder (diamonds k) f path (3 * j)) = 2 ^ (j + 2) - 3.
Proof.
  induction j as [| j IH]; intros k f path Ljk Lf Hp.
  - destruct f as [| f]; [lia |].
    assert (M : mem (3 * 0) path = false) by (apply mem_above; exact Hp).
    assert (G : assoc (diamonds k) (3 * 0) = Some (NUInt 0)) by (rewrite (assoc_diamonds k 0); [reflexivity | lia | lia]).
    rewrite (back_targets_step _ _ _ _ _ M G), (preorder_step _ _ _ _ _ M G). split; reflexivity.
  - destruct f as [| [| f]]; [lia | lia |].
    replace (3 * S j) with (3 * j + 3) by lia.
    assert (M : mem (3 * j + 3) path = false) by (apply mem_above; intros y Hy; specialize (Hp y Hy); lia).
    assert (G : assoc (diamonds k) (3 * j + 3) = Some (NArr [3 * j + 2; 3 * j + 1])).
    { rewrite (assoc_diamonds k (S j)); [| lia | lia]. cbn [diamonds assoc]. rewrite Nat.eqb_refl. reflexivity. }
    assert (Ma : mem (3 * j + 2) (3 * j + 3 :: path) = false) by (apply mem_above; intros y [<- | Hy]; [lia | specialize (Hp y Hy); lia]).
    assert (Ga : assoc (diamonds k) (3 * j + 2) = Some (NArr [3 * j])).
    { rewrite (assoc_diamonds k (S j)); [| lia | lia]. cbn [diamonds assoc].
      replace (Nat.eqb (3 * j + 3) (3 * j + 2)) with false by (symmetry; apply Nat.eqb_neq; lia).
      rewrite Nat.eqb_refl. reflexivity. }
    assert (Mb : mem (3 * j + 1) (3 * j + 3 :: path) = false) by (apply mem_above; intros y [<- | Hy]; [lia | specialize (Hp y Hy); lia]).
    assert (Gb : assoc (diamonds k) (3 * j + 1) = Some (NArr [3 * j])).
    { rewrite (assoc_diamonds k (S j)); [| lia | lia]. cbn [diamonds assoc].
      replace (Nat.eqb (3 * j + 3) (3 * j + 1)) with false by (symmetry; apply Nat.eqb_neq; lia).
      replace (Nat.eqb (3 * j + 2) (3 * j + 1)) with false by (symmetry; apply Nat.eqb_neq; lia).
      rewrite Nat.eqb_refl. reflexivity. }
    assert (Ha := IH k f (3 * j + 2 :: 3 * j + 3 :: path) ltac:(lia) ltac:(lia)
                    ltac:(intros y [<- | [<- | Hy]]; [lia | lia | specialize (Hp y Hy); lia])).
    assert (Hb := IH k f (3 * j + 1 :: 3 * j + 3 :: path) ltac:(lia) ltac:(lia)
                    ltac:(intros y [<- | [<- | Hy]]; [lia | lia | specialize (Hp y Hy); lia])).
    destruct Ha as [Ba La]. destruct Hb as [Bb Lb].
    rewrite (back_targets_step _ _ _ _ _ M G), (preorder_step _ _ _ _ _ M G). cbn [children flat_map].
    rewrite (back_targets_step _ _ _ _ _ Ma Ga), (back_targets_step _ _ _ _ _ Mb Gb).
    rewrite (preorder_step _ _ _ _ _ Ma Ga), (preorder_step _ _ _ _ _ Mb Gb). cbn [children flat_map].
    rewrite Ba, Bb. split; [reflexivity |].
    rewrite !app_nil_r. cbn [length app]. rewrite !length_app. cbn [length]. rewrite La, Lb.
    assert (P4 : 4 <= 2 ^ (j + 2)) by (rewrite Nat.pow_add_r; pose proof (pow2_pos j); simpl; lia).
    replace (S j + 2) with (S (j + 2)) by lia. rewrite Nat.pow_succ_r'. lia.
Qed.

Lemma filter_mem_nil : forall l, filter (fun v => mem v []) l = [].
Proof. induction l as [| x l IH]; simpl; [reflexivity | exact IH]. Qed.

(* T10, the general equation: a chain of k diamonds writes 2^(k+2) - 3 heads. The graph has 3k + 1 nodes. *)
Theorem diamonds_size : forall k, length (write (diamonds k) (3 * k)) = 2 ^ (k + 2) - 3.
Proof.
  intros k. rewrite write_size. unfold unfolded_size, back_edges.
  destruct (diamonds_walk k k (length (diamonds k)) [] (le_n k) ltac:(rewrite length_diamonds; lia) ltac:(intros y []))
    as [B L].
  rewrite B. cbn [nodup]. rewrite filter_mem_nil, L. simpl. lia.
Qed.
